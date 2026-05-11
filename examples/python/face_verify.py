#!/usr/bin/env python3
"""Face verification: are these two images the same person?

Usage:
    python examples/python/face_verify.py <image_a> <image_b>
    python examples/python/face_verify.py alice1.jpg alice2.jpg
    python examples/python/face_verify.py --threshold 0.30 alice.jpg bob.jpg

Models download from GitHub Releases / OpenCV Zoo on first run and are
cached under ~/.cache/naina/. Set NAINA_OFFLINE=1 once you've downloaded
them to skip the network on subsequent runs.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import cv2  # type: ignore[import-untyped]
import numpy as np

import naina


def load_rgb(path: Path) -> np.ndarray:
    bgr = cv2.imread(str(path))
    if bgr is None:
        sys.exit(f"could not read {path}")
    return bgr[:, :, ::-1].copy()  # BGR -> RGB, contiguous


def best_face(faces: list[naina.Face]) -> naina.Face | None:
    if not faces:
        return None
    return max(faces, key=lambda f: f.bbox.score)


def main() -> int:
    ap = argparse.ArgumentParser(description="naina face verification")
    ap.add_argument("image_a", type=Path)
    ap.add_argument("image_b", type=Path)
    ap.add_argument("--threshold", type=float, default=0.36,
                    help="cosine-similarity cutoff for same-person decision "
                         "(SFace's recommended threshold is 0.363)")
    args = ap.parse_args()

    engine = naina.Engine()

    img_a = load_rgb(args.image_a)
    img_b = load_rgb(args.image_b)

    faces_a = engine.detect_faces(img_a)
    faces_b = engine.detect_faces(img_b)
    print(f"{args.image_a}: {len(faces_a)} face(s)")
    print(f"{args.image_b}: {len(faces_b)} face(s)")

    face_a = best_face(faces_a)
    face_b = best_face(faces_b)
    if face_a is None or face_b is None:
        sys.exit("no face detected in one of the inputs")

    emb_a = engine.embed_face(img_a, face_a)
    emb_b = engine.embed_face(img_b, face_b)
    sim = naina.similarity(emb_a, emb_b)

    verdict = "SAME PERSON" if sim >= args.threshold else "different people"
    print(f"\ncosine similarity: {sim:.4f}")
    print(f"threshold:         {args.threshold:.4f}")
    print(f"verdict:           {verdict}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
