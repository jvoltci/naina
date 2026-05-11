// SFace face embedding running on onnxruntime-web.
//
// Model: OpenCV Zoo's face_recognition_sface_2021dec.onnx
// Input:  [1, 3, 112, 112] float32, BGR, normalized to [-1, 1] via (x - 127.5) / 127.5
// Output: [1, 128] float32 embedding. We L2-normalize before returning.
//
// Note: SFace produces 128-d embeddings, not 512-d like ArcFace/EdgeFace.
// Our gallery is dim-agnostic — we store whatever dim the embed model
// returns. When naina's native lib swaps in EdgeFace (512-d), the gallery
// upgrades automatically; existing enrolments are invalidated and the
// user re-enrols.

import * as ort from 'onnxruntime-web';
import { alignFace } from './align';
import type { Face } from './types';

export class FaceEmbedder {
  readonly dim: number;

  private constructor(
    private readonly session: ort.InferenceSession,
    private readonly inputName: string,
    private readonly outputName: string,
    dim: number,
  ) {
    this.dim = dim;
  }

  static async load(modelUrl: string): Promise<FaceEmbedder> {
    const session = await ort.InferenceSession.create(modelUrl, {
      executionProviders: ['webgpu', 'wasm'],
      graphOptimizationLevel: 'all',
    });
    const inputName  = session.inputNames[0];
    const outputName = session.outputNames[0];

    // Probe output dim with a zero tensor.
    const probe = new ort.Tensor('float32', new Float32Array(3 * 112 * 112), [1, 3, 112, 112]);
    const out = await session.run({ [inputName]: probe });
    const dim = out[outputName].dims.slice(-1)[0];

    return new FaceEmbedder(session, inputName, outputName, dim);
  }

  async embed(
    source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
    face: Face,
  ): Promise<Float32Array> {
    const aligned = alignFace(source, face.landmarks, 112);

    // RGBA → NCHW BGR, normalized to [-1, 1].
    const tensor = new Float32Array(3 * 112 * 112);
    const plane = 112 * 112;
    for (let i = 0, p = 0; i < aligned.data.length; i += 4, p++) {
      tensor[p]               = (aligned.data[i + 2] - 127.5) / 127.5;  // B
      tensor[p + plane]       = (aligned.data[i + 1] - 127.5) / 127.5;  // G
      tensor[p + 2 * plane]   = (aligned.data[i    ] - 127.5) / 127.5;  // R
    }

    const out = await this.session.run({
      [this.inputName]: new ort.Tensor('float32', tensor, [1, 3, 112, 112]),
    });
    const raw = out[this.outputName].data as Float32Array;

    // L2 normalize so cosine similarity == dot product.
    return l2Normalize(raw);
  }
}

export function l2Normalize(v: Float32Array): Float32Array {
  let sum = 0;
  for (let i = 0; i < v.length; i++) sum += v[i] * v[i];
  const norm = Math.sqrt(sum) || 1;
  const out = new Float32Array(v.length);
  for (let i = 0; i < v.length; i++) out[i] = v[i] / norm;
  return out;
}

export function cosineSimilarity(a: Float32Array, b: Float32Array): number {
  if (a.length !== b.length) return 0;
  // For L2-normalized vectors, cosine = dot product.
  let d = 0;
  for (let i = 0; i < a.length; i++) d += a[i] * b[i];
  return d;
}
