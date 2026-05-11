#!/usr/bin/env python3
"""End-to-end latency benchmark for naina.

Times face_detect (and face_embed if a face is detected) on a given image
and writes a JSON record with p50/p95/p99 statistics, plus host info, to
the results directory. The runner reads those JSONs to produce the README
benchmark table.

Usage:
    python benchmarks/latency.py --image path/to/face.jpg --target pi5
    python benchmarks/latency.py --target laptop      # synthetic 640x480
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np


def load_image(path: Path | None) -> np.ndarray:
    """Load an image as HxWx3 uint8 RGB, or generate synthetic noise."""
    if path is None:
        rng = np.random.default_rng(seed=0)
        return rng.integers(0, 256, size=(480, 640, 3), dtype=np.uint8)
    try:
        import cv2  # type: ignore[import-not-found]
    except ImportError:
        print("warn: opencv-python not installed; using synthetic image", file=sys.stderr)
        return load_image(None)
    bgr = cv2.imread(str(path))
    if bgr is None:
        raise SystemExit(f"could not read image {path}")
    return bgr[:, :, ::-1].copy()  # BGR -> RGB, contiguous


def percentiles(samples: list[float], qs: tuple[float, ...] = (50, 95, 99)) -> dict[str, float]:
    if not samples:
        return {f"p{int(q)}": 0.0 for q in qs}
    return {f"p{int(q)}": float(np.percentile(samples, q)) for q in qs}


def time_calls(fn, warmup: int, iters: int) -> list[float]:
    for _ in range(warmup):
        fn()
    samples_ms: list[float] = []
    for _ in range(iters):
        t0 = time.perf_counter()
        fn()
        samples_ms.append((time.perf_counter() - t0) * 1000.0)
    return samples_ms


def system_info() -> dict[str, str]:
    return {
        "system": platform.system(),
        "machine": platform.machine(),
        "processor": platform.processor() or platform.machine(),
        "python": sys.version.split(" ")[0],
        "hostname": platform.node(),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="naina latency benchmark")
    ap.add_argument("--image", type=Path, default=None,
                    help="path to a test image (default: synthetic 640x480 noise)")
    ap.add_argument("--target", required=True,
                    help='label for this benchmark run (e.g. "pi5", "m3-pro", "x86-rtx4090")')
    ap.add_argument("--warmup", type=int, default=5)
    ap.add_argument("--iters", type=int, default=50)
    ap.add_argument("--research", action="store_true",
                    help="opt into the research-tier model weights")
    ap.add_argument("--out", type=Path, default=Path("benchmarks/results"))
    args = ap.parse_args()

    try:
        import naina
    except ImportError:
        print("error: install naina first (pip install -e .)", file=sys.stderr)
        return 2

    print(f"naina {naina.__version__}")
    print(f"target = {args.target}")
    img = load_image(args.image)
    print(f"image  = {img.shape[1]}x{img.shape[0]}")

    engine = naina.Engine(enable_research_models=args.research)

    # Warm up the lazy session load + first JIT compile.
    print("warming up…")
    faces = engine.detect_faces(img)
    print(f"detected {len(faces)} face(s) in warmup frame")

    print(f"timing detect (warmup={args.warmup}, iters={args.iters})…")
    detect_ms = time_calls(lambda: engine.detect_faces(img), args.warmup, args.iters)
    detect_stats = {
        "mean": statistics.fmean(detect_ms),
        "stdev": statistics.pstdev(detect_ms),
        **percentiles(detect_ms),
        "n": len(detect_ms),
    }
    print(f"detect: p50={detect_stats['p50']:.1f}ms  p95={detect_stats['p95']:.1f}ms")

    embed_stats: dict[str, Any] | None = None
    if faces:
        print(f"timing embed (per face, warmup={args.warmup}, iters={args.iters})…")
        face = faces[0]
        embed_ms = time_calls(lambda: engine.embed_face(img, face), args.warmup, args.iters)
        embed_stats = {
            "mean": statistics.fmean(embed_ms),
            "stdev": statistics.pstdev(embed_ms),
            **percentiles(embed_ms),
            "n": len(embed_ms),
            "embedding_dim": engine.face_embed_dim(),
        }
        print(f"embed:  p50={embed_stats['p50']:.1f}ms  p95={embed_stats['p95']:.1f}ms")
    else:
        print("(no face found in input — skipping embed timing)")

    record = {
        "naina_version": naina.__version__,
        "target": args.target,
        "tier": "research" if args.research else "default",
        "system": system_info(),
        "image": {"width": int(img.shape[1]), "height": int(img.shape[0])},
        "detect_ms": detect_stats,
        "embed_ms": embed_stats,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }

    args.out.mkdir(parents=True, exist_ok=True)
    out_path = args.out / f"{args.target}-{record['tier']}.json"
    out_path.write_text(json.dumps(record, indent=2))
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
