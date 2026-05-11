// naina-web Engine — façade that mirrors the planned naina.Engine API.
//
// This is the same surface a developer will use against naina's native
// (Python/Node) or WASM bindings later. When naina-wasm lands, this file
// is swapped for a wrapper around the native WASM module — the UI code
// (main.ts) doesn't change.

import * as ort from 'onnxruntime-web';
import type { EngineOptions, Face, FrameResult, RecognitionMatch } from './types';
import { FaceDetector } from './detect';
import { FaceEmbedder } from './embed';
import { Gallery } from './gallery';

export class Engine {
  private constructor(
    private readonly detector: FaceDetector,
    private readonly embedder: FaceEmbedder,
    readonly gallery: Gallery,
    readonly backend: string,
  ) {}

  static async create(opts: EngineOptions = {}): Promise<Engine> {
    const base = (opts.modelBase ?? 'models').replace(/\/$/, '');
    const inputSize = opts.detectorInputSize ?? 320;

    // Best-effort backend negotiation report (onnxruntime-web picks
    // automatically; we just report what was probably used).
    const backend = (await detectBackend());

    const [detector, embedder] = await Promise.all([
      FaceDetector.load(`${base}/yunet.onnx`, {
        inputSize,
        confThreshold: opts.confidenceThreshold ?? 0.6,
        nmsIou: opts.nmsIouThreshold ?? 0.3,
      }),
      FaceEmbedder.load(`${base}/sface.onnx`),
    ]);

    return new Engine(detector, embedder, new Gallery(), backend);
  }

  async detectFaces(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
  ): Promise<Face[]> {
    return this.detector.detect(source);
  }

  async embedFace(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
    face: Face,
  ): Promise<Float32Array> {
    return this.embedder.embed(source, face);
  }

  // One-shot: detect → embed each → match each → return everything.
  // For N faces this is N+1 inferences (1 detect + N embeds).
  async recognizeFrame(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
    threshold: number,
  ): Promise<FrameResult> {
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
  }
}

async function detectBackend(): Promise<string> {
  // onnxruntime-web negotiates WebGPU → WASM internally. We surface a
  // best-guess label for the UI's metrics panel.
  type NavGpu = Navigator & { gpu?: { requestAdapter(): Promise<unknown> } };
  const nav = navigator as NavGpu;
  if (nav.gpu) {
    try {
      const adapter = await nav.gpu.requestAdapter();
      if (adapter) return 'WebGPU';
    } catch { /* fall through */ }
  }
  // onnxruntime-web's WASM EP supports SIMD + threads when available.
  const wasm = ort.env.wasm;
  const parts = ['WASM'];
  if (wasm?.simd) parts.push('SIMD');
  if ((wasm?.numThreads ?? 1) > 1) parts.push(`x${wasm.numThreads}`);
  return parts.join(' · ');
}
