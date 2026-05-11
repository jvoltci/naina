// 5-point similarity face alignment.
//
// Canonical reference points for 112x112 ArcFace/SFace-style models.
// We solve for similarity T (rotation + uniform scale + translation) that
// maps detected landmarks → reference points, then warp the source image
// through that transform onto a 112x112 canvas.

import type { Point } from './types';

export const REF_KPS_112: ReadonlyArray<Point> = [
  { x: 38.2946, y: 51.6963 },   // left eye
  { x: 73.5318, y: 51.5014 },   // right eye
  { x: 56.0252, y: 71.7366 },   // nose
  { x: 41.5493, y: 92.3655 },   // left mouth
  { x: 70.7299, y: 92.2041 },   // right mouth
];

interface Similarity { a: number; b: number; e: number; f: number; }
// Matrix: [ a  -b  e ]    => x' = a*x - b*y + e
//         [ b   a  f ]       y' = b*x + a*y + f

// Umeyama-style closed-form solve in complex form. Both arrays must
// have length 5 (or matching length ≥ 2).
function solveSimilarity(src: ReadonlyArray<Point>, dst: ReadonlyArray<Point>): Similarity {
  const n = src.length;
  let mxs = 0, mys = 0, mxd = 0, myd = 0;
  for (let i = 0; i < n; i++) {
    mxs += src[i].x; mys += src[i].y;
    mxd += dst[i].x; myd += dst[i].y;
  }
  mxs /= n; mys /= n; mxd /= n; myd /= n;

  let num_re = 0, num_im = 0, den = 0;
  for (let i = 0; i < n; i++) {
    const px = src[i].x - mxs, py = src[i].y - mys;
    const qx = dst[i].x - mxd, qy = dst[i].y - myd;
    // conj(p) * q
    num_re += px * qx + py * qy;
    num_im += px * qy - py * qx;
    den    += px * px + py * py;
  }
  if (den === 0) return { a: 1, b: 0, e: 0, f: 0 };

  const a = num_re / den;
  const b = num_im / den;
  const e = mxd - (a * mxs - b * mys);
  const f = myd - (b * mxs + a * mys);
  return { a, b, e, f };
}

// Returns a 112x112 RGBA ImageData of the aligned face crop.
export function alignFace(
  source: HTMLVideoElement | HTMLCanvasElement | ImageBitmap,
  landmarks: ReadonlyArray<Point>,
  size = 112,
): ImageData {
  const ref = size === 112
    ? REF_KPS_112
    : REF_KPS_112.map(p => ({ x: p.x * size / 112, y: p.y * size / 112 }));

  const t = solveSimilarity(landmarks, ref);

  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d', { willReadFrequently: true })!;
  ctx.fillStyle = 'black';
  ctx.fillRect(0, 0, size, size);

  // setTransform takes (a, b, c, d, e, f) where
  //   x' = a*x + c*y + e
  //   y' = b*x + d*y + f
  // Our similarity is x' = a*x - b*y + e, y' = b*x + a*y + f
  // → setTransform(a, b, -b, a, e, f)
  ctx.setTransform(t.a, t.b, -t.b, t.a, t.e, t.f);
  ctx.drawImage(source as CanvasImageSource, 0, 0);
  ctx.setTransform(1, 0, 0, 1, 0, 0);

  return ctx.getImageData(0, 0, size, size);
}
