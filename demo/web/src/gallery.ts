// In-browser enrollment gallery.
//
// Stores L2-normalized embeddings keyed by user-provided name. Persisted to
// localStorage so enrolments survive reloads. For N detected faces in a
// frame, recognition is greedy: each face's embedding is compared against
// every gallery entry; best match above threshold wins.
//
// One entry per identity = single template. v1.1 will add multi-template
// per identity (k-NN over multiple captures) for robustness across pose.

import type { EnrolledIdentity, RecognitionMatch } from './types';
import { cosineSimilarity } from './embed';

const STORAGE_KEY = 'naina.gallery.v1';

export class Gallery {
  private items: EnrolledIdentity[] = [];
  private dim = 0;     // 0 = empty; first enrolment fixes the dim

  constructor() {
    this.load();
  }

  size(): number { return this.items.length; }
  list(): readonly EnrolledIdentity[] { return this.items; }

  enrol(name: string, embedding: Float32Array): EnrolledIdentity {
    const trimmed = name.trim();
    if (!trimmed) throw new Error('Name cannot be empty');
    if (this.dim && embedding.length !== this.dim) {
      throw new Error(
        `Embedding dim mismatch (gallery has ${this.dim}-d, got ${embedding.length}-d). ` +
        `Clear the gallery if you changed the recognition model.`,
      );
    }
    this.dim = embedding.length;
    const item: EnrolledIdentity = {
      id: crypto.randomUUID(),
      name: trimmed,
      embedding: new Float32Array(embedding),    // defensive copy
      enrolledAt: Date.now(),
    };
    this.items.push(item);
    this.save();
    return item;
  }

  remove(id: string): void {
    this.items = this.items.filter(i => i.id !== id);
    if (this.items.length === 0) this.dim = 0;
    this.save();
  }

  clear(): void {
    this.items = [];
    this.dim = 0;
    this.save();
  }

  // Match a single embedding against the gallery; returns best candidate
  // (with similarity score) or a null-identity match if gallery is empty
  // or no candidate exceeds threshold.
  match(embedding: Float32Array, threshold: number): RecognitionMatch {
    if (this.items.length === 0) {
      return { identityId: null, name: 'unknown', similarity: 0 };
    }
    let bestId: string | null = null;
    let bestName = 'unknown';
    let bestSim = -1;
    for (const item of this.items) {
      const sim = cosineSimilarity(embedding, item.embedding);
      if (sim > bestSim) { bestSim = sim; bestId = item.id; bestName = item.name; }
    }
    if (bestSim < threshold) {
      return { identityId: null, name: 'unknown', similarity: bestSim };
    }
    return { identityId: bestId, name: bestName, similarity: bestSim };
  }

  // ── persistence ──────────────────────────────────────────────────────

  private save(): void {
    try {
      const serialized = this.items.map(i => ({
        id: i.id,
        name: i.name,
        enrolledAt: i.enrolledAt,
        // Float32Array → number[] for JSON.
        embedding: Array.from(i.embedding),
      }));
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ dim: this.dim, items: serialized }));
    } catch {
      // localStorage may be full or disabled (private browsing).
    }
  }

  private load(): void {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return;
      const parsed = JSON.parse(raw) as {
        dim: number;
        items: Array<{ id: string; name: string; enrolledAt: number; embedding: number[] }>;
      };
      this.dim = parsed.dim;
      this.items = parsed.items.map(i => ({
        id: i.id,
        name: i.name,
        enrolledAt: i.enrolledAt,
        embedding: new Float32Array(i.embedding),
      }));
    } catch {
      this.items = [];
      this.dim = 0;
    }
  }
}
