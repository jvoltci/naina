import type { Face } from './types';

function iou(a: Face['bbox'], b: Face['bbox']): number {
  const x1 = Math.max(a.x, b.x);
  const y1 = Math.max(a.y, b.y);
  const x2 = Math.min(a.x + a.w, b.x + b.w);
  const y2 = Math.min(a.y + a.h, b.y + b.h);
  const inter = Math.max(0, x2 - x1) * Math.max(0, y2 - y1);
  const uni = a.w * a.h + b.w * b.h - inter;
  return uni > 0 ? inter / uni : 0;
}

// Greedy NMS. Returns kept faces (by score desc).
export function nms(faces: Face[], iouThreshold: number): Face[] {
  const sorted = [...faces].sort((p, q) => q.bbox.score - p.bbox.score);
  const kept: Face[] = [];
  for (const f of sorted) {
    let overlaps = false;
    for (const k of kept) {
      if (iou(f.bbox, k.bbox) > iouThreshold) { overlaps = true; break; }
    }
    if (!overlaps) kept.push(f);
  }
  return kept;
}
