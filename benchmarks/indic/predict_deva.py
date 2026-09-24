"""Read every crop in a folder with naina, one tier, one alphabet; write the predictions
in ground-truth order as a JSON list of strings (the shape the paper's report script
reads) plus per-crop timing.

naina is a page reader: it detects text lines, then recognises them. A word crop is
a degenerate page, and the paper itself flags that the word-level format disadvantages
page-oriented systems. Two things are therefore reported loudly rather than averaged
away: crops where detection found nothing (the prediction is empty and scores CER 100),
and, with --pad, the same run with each crop pasted on a white canvas with a margin,
which is the preprocessing a page reader needs to see a lone word as a line.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from datetime import date
from pathlib import Path

import numpy as np
from PIL import Image

import naina

TIERS = {"tiny": "TINY", "small": "SMALL", "medium": "MEDIUM"}


def padded(img: Image.Image, margin_ratio: float) -> Image.Image:
    m = max(8, int(img.height * margin_ratio))
    canvas = Image.new("RGB", (img.width + 2 * m, img.height + 2 * m), (255, 255, 255))
    canvas.paste(img, (m, m))
    return canvas


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", default="build/indic/deva300", help="folder with images/ and gt.json")
    ap.add_argument("--tier", required=True, choices=sorted(TIERS))
    ap.add_argument("--language", default="devanagari")
    ap.add_argument("--pad", type=float, default=0.0,
                    help="margin as a fraction of crop height pasted around each crop on white; 0 = raw crop")
    ap.add_argument("--out", default="")
    a = ap.parse_args()

    root = Path(a.set)
    gt = json.loads((root / "gt.json").read_text(encoding="utf-8"))
    paths = [root / "images" / f"{i:04d}.png" for i in range(len(gt))]
    missing = [p for p in paths if not p.exists()]
    if missing:
        sys.exit(f"{len(missing)} crops missing, first {missing[0]}; rerun fetch_deva300.py")

    tier = getattr(naina.Tier, TIERS[a.tier])
    try:
        engine = naina.Engine(tier=tier, language=a.language)
    except Exception as e:  # noqa: BLE001
        sys.exit(f"naina {naina.__version__} cannot build tier {a.tier} with language "
                 f"{a.language!r}: {e}")

    tag = f"{a.tier}" + (f"_pad{a.pad:g}" if a.pad else "")
    out = Path(a.out) if a.out else root / f"pred_{tag}.json"
    preds: list[str] = []
    ms: list[float] = []
    empties = 0
    for i, p in enumerate(paths):
        img = Image.open(p).convert("RGB")
        if a.pad:
            img = padded(img, a.pad)
        arr = np.asarray(img, dtype=np.uint8)
        t0 = time.perf_counter()
        page = engine.read(arr)
        ms.append((time.perf_counter() - t0) * 1000)
        text = " ".join(line.text for line in page.lines if line.text).strip()
        if not text:
            empties += 1
        preds.append(text)
        if i % 50 == 0:
            print(f"  {i:4d}/{len(paths)}  {ms[-1]:6.0f} ms  {text[:40]!r}  gt {gt[i][:40]!r}", flush=True)

    out.write_text(json.dumps(preds, ensure_ascii=False, indent=0), encoding="utf-8")
    warm = sorted(ms[1:]) or ms
    summary = {
        "naina_version": naina.__version__, "tier": a.tier, "language": a.language,
        "pad": a.pad, "n": len(preds), "empty_predictions": empties,
        "cold_first_ms": round(ms[0], 1), "warm_median_ms": round(statistics.median(warm), 1),
        "warm_p95_ms": round(warm[min(len(warm) - 1, int(0.95 * len(warm)))], 1),
        "date": date.today().isoformat(),
    }
    out.with_name(out.stem + "_timing.json").write_text(json.dumps(summary, indent=1))
    print(json.dumps(summary))
    if empties:
        print(f"WARNING: {empties} of {len(preds)} crops produced no text (detection found no line)",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
