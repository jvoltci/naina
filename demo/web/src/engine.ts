// naina-web Engine — façade that mirrors the planned naina.Engine API.
//
// This is the same surface a developer will use against naina's native
// (Python/Node) or WASM bindings later. When naina-wasm lands, this file
// is swapped for a wrapper around the native WASM module — the UI code
// (main.ts) doesn't change.

import type { EngineOptions, Face, FrameResult, RecognitionMatch } from './types';
import { FaceDetector } from './detect';
import { FaceEmbedder } from './embed';
import { Gallery } from './gallery';

export class Engine {
  // Single-flight lock: ONNX sessions are not safe to call concurrently.
  // Every public method that touches a session queues behind this.
  private inflight: Promise<unknown> = Promise.resolve();

  private constructor(
    private readonly detector: FaceDetector,
    private readonly embedder: FaceEmbedder,
    readonly gallery: Gallery,
    readonly backend: string,
  ) {}

  static async create(opts: EngineOptions = {}): Promise<Engine> {
    const base = (opts.modelBase ?? 'models').replace(/\/$/, '');
    const inputSize = opts.detectorInputSize ?? 640;

    const [detector, embedder] = await Promise.all([
      FaceDetector.load(`${base}/yunet.onnx`, {
        inputSize,
        confThreshold: opts.confidenceThreshold ?? 0.6,
        nmsIou: opts.nmsIouThreshold ?? 0.3,
      }),
      FaceEmbedder.load(`${base}/sface.onnx`),
    ]);

    return new Engine(detector, embedder, new Gallery(), 'WASM · SIMD');
  }

  async detectFaces(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
  ): Promise<Face[]> {
    return this.serial(() => this.detector.detect(source));
  }

  async embedFace(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
    face: Face,
  ): Promise<Float32Array> {
    return this.serial(() => this.embedder.embed(source, face));
  }

  // One-shot: detect → embed each → match each → return everything.
  // For N faces this is N+1 inferences (1 detect + N embeds).
  async recognizeFrame(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
    threshold: number,
  ): Promise<FrameResult> {
    return this.serial(async () => {
      const detectStart = performance.now();
      const faces = await this.detector.detect(source);
      const detectMs = performance.now() - detectStart;

      const matches: RecognitionMatch[] = new Array(faces.length);
      const embedStart = performance.now();
      for (let i = 0; i < faces.length; i++) {
        const emb = await this.embedder.embed(source, faces[i]);
        matches[i] = this.gallery.match(emb, threshold);
      }
      const totalEmbedMs = performance.now() - embedStart;
      const embedMsPerFace = faces.length > 0 ? totalEmbedMs / faces.length : 0;

      return { faces, matches, timings: { detectMs, embedMsPerFace } };
    });
  }

  // Chain `fn` onto the in-flight queue. Errors don't poison the chain.
  private serial<T>(fn: () => Promise<T>): Promise<T> {
    const next = this.inflight.then(fn, fn);
    // Keep the chain alive on error so subsequent calls aren't rejected.
    this.inflight = next.catch(() => undefined);
    return next;
  }
}

