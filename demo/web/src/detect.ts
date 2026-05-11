// YuNet face detector running on onnxruntime-web.
//
// Model: OpenCV Zoo's face_detection_yunet_2023mar.onnx
// Inputs:  "input"  — [1, 3, H, W] float32, BGR, raw 0-255 (no normalization)
// Outputs: cls_{s}, obj_{s}, bbox_{s}, kps_{s} for s in {8, 16, 32}
//          shapes: cls/obj = [1, N, 1], bbox = [1, N, 4], kps = [1, N, 10]
//
// Decoding (anchor-free, anchor at each grid cell center):
//   score   = sqrt(cls * obj)
//   cx      = (gx + 0.5 + bbox[0]) * stride
//   cy      = (gy + 0.5 + bbox[1]) * stride
//   w       = exp(bbox[2]) * stride
//   h       = exp(bbox[3]) * stride
//   kp_k.x  = (gx + 0.5 + kps[2k])   * stride
//   kp_k.y  = (gy + 0.5 + kps[2k+1]) * stride

import * as ort from 'onnxruntime-web';
import type { Face } from './types';
import { nms } from './nms';

const STRIDES = [8, 16, 32] as const;
const NUM_KPS = 5;

export interface DetectOptions {
  inputSize: number;          // square, multiple of 32 (e.g. 320, 480, 640)
  confThreshold: number;
  nmsIou: number;
}

interface Letterbox {
  scale: number;     // = inputSize / max(origW, origH)
  padX: number;
  padY: number;
  origW: number;
  origH: number;
}

export class FaceDetector {
  private constructor(
    private readonly session: ort.InferenceSession,
    private readonly opts: DetectOptions,
  ) {}

  static async load(modelUrl: string, opts: DetectOptions): Promise<FaceDetector> {
    const session = await ort.InferenceSession.create(modelUrl, {
      executionProviders: ['webgpu', 'wasm'],
      graphOptimizationLevel: 'all',
    });
    return new FaceDetector(session, opts);
  }

  async detect(source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap): Promise<Face[]> {
    const { inputSize } = this.opts;

    // 1) Letterbox onto a square canvas at inputSize.
    const { tensor, letterbox } = this.preprocess(source);

    // 2) Run.
    const outputs = await this.session.run({ input: tensor });

    // 3) Decode anchor-free outputs across the three strides.
    const raw: Face[] = [];
    for (const s of STRIDES) {
      const cls  = outputs[`cls_${s}`].data  as Float32Array;
      const obj  = outputs[`obj_${s}`].data  as Float32Array;
      const bbox = outputs[`bbox_${s}`].data as Float32Array;
      const kps  = outputs[`kps_${s}`].data  as Float32Array;

      const gridW = inputSize / s;
      const gridH = inputSize / s;
      const n = gridW * gridH;

      for (let i = 0; i < n; i++) {
        const score = Math.sqrt(Math.max(0, cls[i] * obj[i]));
        if (score < this.opts.confThreshold) continue;

        const gx = i % gridW;
        const gy = Math.floor(i / gridW);
        const ax = (gx + 0.5);
        const ay = (gy + 0.5);

        const cx = (ax + bbox[i * 4 + 0]) * s;
        const cy = (ay + bbox[i * 4 + 1]) * s;
        const w  = Math.exp(bbox[i * 4 + 2]) * s;
        const h  = Math.exp(bbox[i * 4 + 3]) * s;

        const landmarks: Face['landmarks'] = [
          { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 }, { x: 0, y: 0 },
        ];
        for (let k = 0; k < NUM_KPS; k++) {
          landmarks[k] = {
            x: (ax + kps[i * 10 + 2 * k    ]) * s,
            y: (ay + kps[i * 10 + 2 * k + 1]) * s,
          };
        }

        // Map back from letterboxed coords to original image coords.
        const inv = this.unletterbox(letterbox);
        const face: Face = {
          bbox: {
            x: inv.x(cx - w / 2),
            y: inv.y(cy - h / 2),
            w: w / letterbox.scale,
            h: h / letterbox.scale,
            score,
          },
          landmarks: landmarks.map(p => ({ x: inv.x(p.x), y: inv.y(p.y) })) as Face['landmarks'],
          quality: Math.min(1, score * 1.25),
          trackId: -1,
        };
        raw.push(face);
      }
    }

    return nms(raw, this.opts.nmsIou);
  }

  // ── private ─────────────────────────────────────────────────────────

  private preprocess(
    src: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
  ): { tensor: ort.Tensor; letterbox: Letterbox } {
    const { inputSize } = this.opts;
    const origW =
      src instanceof HTMLVideoElement ? src.videoWidth  :
      src instanceof ImageBitmap     ? src.width        :
                                       src.width;
    const origH =
      src instanceof HTMLVideoElement ? src.videoHeight :
      src instanceof ImageBitmap     ? src.height       :
                                       src.height;

    const scale = inputSize / Math.max(origW, origH);
    const newW = Math.round(origW * scale);
    const newH = Math.round(origH * scale);
    const padX = Math.floor((inputSize - newW) / 2);
    const padY = Math.floor((inputSize - newH) / 2);

    const canvas = document.createElement('canvas');
    canvas.width = inputSize;
    canvas.height = inputSize;
    const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
    ctx.fillStyle = 'black';
    ctx.fillRect(0, 0, inputSize, inputSize);
    ctx.drawImage(src as CanvasImageSource, padX, padY, newW, newH);

    const { data } = ctx.getImageData(0, 0, inputSize, inputSize);
    // NCHW, BGR, float32 raw 0-255
    const out = new Float32Array(3 * inputSize * inputSize);
    const plane = inputSize * inputSize;
    for (let i = 0, p = 0; i < data.length; i += 4, p++) {
      out[p]              = data[i + 2];  // B
      out[p + plane]      = data[i + 1];  // G
      out[p + 2 * plane]  = data[i];      // R
    }

    return {
      tensor: new ort.Tensor('float32', out, [1, 3, inputSize, inputSize]),
      letterbox: { scale, padX, padY, origW, origH },
    };
  }

  private unletterbox(l: Letterbox) {
    return {
      x: (px: number) => (px - l.padX) / l.scale,
      y: (py: number) => (py - l.padY) / l.scale,
    };
  }
}
