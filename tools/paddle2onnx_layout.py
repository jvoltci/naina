#!/usr/bin/env python3
"""Export PP-DocLayout-S / -M / -L to ONNX, and prove the export is faithful.

Why this exists
---------------
PaddleOCR publishes an ONNX build of PP-DocLayoutV3 but not of the smaller
PP-DocLayout-S and -M. Without them, layout analysis would be available only
at naina's ``medium`` tier (269 MB), and the ``tiny`` tier could not claim
document structure at all — which is the whole point of an 11 MB build that
runs in a browser tab.

These weights ship in Paddle's format (``inference.json`` graph +
``inference.pdiparams``), which ``paddle2onnx`` can convert.

Faithfulness
------------
An export that silently drifts is worse than no export, so ``verify`` runs the
Paddle model and the ONNX model on the same realistic page and compares
outputs **per column**, because the output packs values of very different
magnitude:

    [class_id, score, x1, y1, x2, y2]

A single blanket tolerance is the wrong test — 1e-3 is meaningless on a pixel
coordinate and significant on a class id. Measured on PP-DocLayout-S:
classes match exactly, scores to 5e-7, coordinates to 3e-4 of a pixel.

The export is also **byte-deterministic**: re-running it produces an identical
sha256, so the artifact naina publishes can be independently reproduced.

Interface note
--------------
The three variants do not share an input signature, though they do share an
output format:

    S   image[1,3,480,480], scale_factor[1,2]              -> [N,6], [1]
    M   image[1,3,640,640], scale_factor[1,2]              -> [N,6], [1]
    L   im_shape[1,2], image[1,3,640,640], scale_factor[1,2] -> [N,6], [N]
    V3  im_shape, image[1,3,800,800], scale_factor         -> [N,6], [N], masks

So the C++ module must feed inputs **by name** from ``session->inputs()``
rather than positionally. All four emit the same ``[N, 6]`` detection rows, so
post-processing is uniform. S and M carry NMS inside the graph, which caps
them at batch_size == 1 — irrelevant here, since layout runs one page at a
time.

Usage
-----
    pip install paddlepaddle paddle2onnx onnx onnxruntime pillow
    python tools/paddle2onnx_layout.py export
    python tools/paddle2onnx_layout.py verify
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
STAGING = REPO_ROOT / "build" / "layout-export"
HF = "https://huggingface.co/PaddlePaddle"

# variant -> (upstream repo, fixed input side, expected onnx sha256, expected bytes)
# Hashes recorded from a verified deterministic export.
VARIANTS: dict[str, tuple[str, int, str, int]] = {
    "S": (
        "PP-DocLayout-S",
        480,
        "f905a530dd36fe4ab6613457a52e75bca2bb8c3e3d53eb4cb902e21a2d4c2548",
        4904714,
    ),
    "M": (
        "PP-DocLayout-M",
        640,
        "7ae030241e5780af8736c077d3c5ce700ca83589c251fd962ee2234de2343947",
        23484258,
    ),
    "L": (
        "PP-DocLayout-L",
        640,
        "4c5efa9a4c3682e1ff8fe16e00f1a84d240e3df25867b6223d933685bd5e18b0",
        129398697,
    ),
}

SRC_FILES = ("inference.json", "inference.pdiparams", "inference.yml")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download_source(variant: str) -> Path:
    repo, _, _, _ = VARIANTS[variant]
    out = STAGING / repo
    out.mkdir(parents=True, exist_ok=True)
    for name in SRC_FILES:
        dest = out / name
        if dest.exists() and dest.stat().st_size > 0:
            continue
        print(f"    fetch {repo}/{name}")
        urllib.request.urlretrieve(f"{HF}/{repo}/resolve/main/{name}", dest)  # noqa: S310
    return out


def export() -> int:
    STAGING.mkdir(parents=True, exist_ok=True)
    failures = 0
    for variant, (repo, side, want_sha, want_bytes) in VARIANTS.items():
        print(f"\nPP-DocLayout-{variant}  (input {side}x{side})")
        src = download_source(variant)
        dest = STAGING / f"ppdoclayout_{variant.lower()}.onnx"

        cmd = [
            sys.executable, "-m", "paddle2onnx",
            "--model_dir", str(src),
            "--model_filename", "inference.json",
            "--params_filename", "inference.pdiparams",
            "--save_file", str(dest),
            "--opset_version", "14",
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if proc.returncode != 0 or not dest.exists():
            print(f"    EXPORT FAILED\n{proc.stdout}\n{proc.stderr}")
            failures += 1
            continue

        got_sha = sha256_file(dest)
        got_bytes = dest.stat().st_size
        match = got_sha == want_sha and got_bytes == want_bytes
        print(f"    -> {dest.name}  {got_bytes} bytes")
        print(f"    sha256 {got_sha}")
        if match:
            print("    matches the recorded deterministic export")
        else:
            # Not fatal: a newer paddle2onnx may legitimately emit a different
            # graph. But the recorded hash is what naina publishes, so the
            # difference must be understood before shipping.
            print("    DIFFERS from the recorded hash — investigate before publishing")
            print(f"    expected {want_sha}  {want_bytes} bytes")
            failures += 1
    return 1 if failures else 0


def verify() -> int:
    """Compare Paddle and ONNX outputs per column on a realistic page."""
    try:
        import numpy as np
        import onnxruntime as ort
        from paddle import inference as pi
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as e:
        print(f"verify needs paddlepaddle, onnxruntime, pillow, numpy: {e}", file=sys.stderr)
        return 1

    def find_font(size: int):
        for p in (
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "C:/Windows/Fonts/arial.ttf",
        ):
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                continue
        return None

    font = find_font(26)
    if font is None:
        print("no scalable font found; cannot render a realistic page", file=sys.stderr)
        return 1

    overall = 0
    for variant, (repo, side, _, _) in VARIANTS.items():
        onnx_path = STAGING / f"ppdoclayout_{variant.lower()}.onnx"
        src = STAGING / repo
        if not onnx_path.exists():
            print(f"PP-DocLayout-{variant}: {onnx_path.name} missing — run `export` first")
            overall = 1
            continue

        # A document-like page. Random noise produces near-tied NMS candidates
        # whose ordering is genuinely unstable, which would make any comparison
        # meaningless.
        page = Image.new("RGB", (side, side), (255, 255, 255))
        d = ImageDraw.Draw(page)
        d.text((30, 24), "Quarterly Report", fill=(0, 0, 0), font=font)
        for i in range(6):
            d.rectangle([30, 90 + i * 22, side - 30, 90 + i * 22 + 11], fill=(60, 60, 60))
        d.rectangle([30, 260, side - 30, min(400, side - 40)], outline=(0, 0, 0), width=2)

        arr = np.asarray(page).astype(np.float32) / 255.0
        arr = (arr - np.array([0.485, 0.456, 0.406], np.float32)) / np.array(
            [0.229, 0.224, 0.225], np.float32
        )
        image = np.ascontiguousarray(arr.transpose(2, 0, 1)[None])
        scale_factor = np.array([[1.0, 1.0]], dtype=np.float32)
        im_shape = np.array([[float(side), float(side)]], dtype=np.float32)
        feed_all = {"image": image, "scale_factor": scale_factor, "im_shape": im_shape}

        cfg = pi.Config(str(src / "inference.json"), str(src / "inference.pdiparams"))
        cfg.disable_gpu()
        cfg.disable_glog_info()
        pred = pi.create_predictor(cfg)
        for name in pred.get_input_names():
            pred.get_input_handle(name).copy_from_cpu(feed_all[name])
        pred.run()
        p_out = [pred.get_output_handle(n).copy_to_cpu() for n in pred.get_output_names()]

        sess = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
        o_feed = {i.name: feed_all[i.name] for i in sess.get_inputs()}
        o_out = sess.run(None, o_feed)

        boxes_p, boxes_o = p_out[0], o_out[0]
        n = min(int(p_out[1].reshape(-1)[0]), int(o_out[1].reshape(-1)[0]))
        A, B = boxes_p[:n], boxes_o[:n]

        # Per-column tolerances: a blanket epsilon is the wrong test when the
        # columns are a class id, a 0..1 score, and pixel coordinates.
        cols = (("class", 0.0), ("score", 1e-3), ("x1", 0.05), ("y1", 0.05),
                ("x2", 0.05), ("y2", 0.05))
        print(f"\nPP-DocLayout-{variant}: {n} detections compared")
        ok = A.shape == B.shape
        if not ok:
            print(f"    SHAPE MISMATCH {A.shape} vs {B.shape}")
        for c, (label, tol) in enumerate(cols):
            diff = np.abs(A[:, c].astype(np.float64) - B[:, c].astype(np.float64))
            m = float(diff.max()) if diff.size else 0.0
            good = (m == 0.0) if tol == 0.0 else (m < tol)
            ok &= good
            print(f"    {label:6} max_abs_diff={m:.3e}  {'ok' if good else 'FAIL'}")
        print("    FAITHFUL" if ok else "    DIVERGES — do not publish this export")
        if not ok:
            overall = 1
    return overall


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("command", choices=("export", "verify"))
    args = ap.parse_args()
    return {"export": export, "verify": verify}[args.command]()


if __name__ == "__main__":
    raise SystemExit(main())
