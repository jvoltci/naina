"""Run naina over a folder of page images, writing what OmniDocBench expects.

One `.md` per page, named as the image with the extension replaced, which is
how the evaluator matches predictions to ground truth. Timing goes beside it
in `_timing.json`: the first call on a cold tier pays the model download and
load, so it is recorded separately and excluded from the warm median.
"""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
import time
from datetime import date
from pathlib import Path

import numpy as np
from PIL import Image

import naina

TIERS = {"tiny": "TINY", "small": "SMALL", "medium": "MEDIUM", "large": "LARGE", "auto": "AUTO"}


def device_string() -> str:
    try:
        cpu = subprocess.run(
            ["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True, timeout=10
        ).stdout.strip()
    except Exception:  # noqa: BLE001
        cpu = platform.processor() or "unknown"
    return f"{cpu}, {platform.system()} {platform.release()}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--images", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tier", default="tiny", choices=sorted(TIERS))
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--runs", type=int, default=1, help="repeat each page, keep the median")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    tier_name = TIERS[a.tier]
    tier = getattr(naina.Tier, tier_name, None)
    if tier is None:
        sys.exit(f"this naina ({naina.__version__}) has no tier {tier_name}; it has "
                 f"{[t for t in dir(naina.Tier) if t.isupper()]}")

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    paths = sorted(p for p in Path(a.images).iterdir() if p.suffix.lower() in (".jpg", ".jpeg", ".png"))
    if a.limit:
        paths = paths[: a.limit]
    if not paths:
        sys.exit(f"no images in {a.images}")

    print(f"naina {naina.__version__} | tier {a.tier} | {len(paths)} pages | {device_string()}")

    # One Engine for the whole run. `naina.read()` constructs a throwaway Engine
    # on every call, which its own docstring warns about and which this harness
    # missed on 2026-09-22: measured at about 330 ms per page at tiny and
    # 5,300 ms per page at medium, all of it model reload. Every "warm median"
    # published before this line was inflated by that amount.
    def make_engine():
        for ctor in (lambda: naina.Engine(tier=tier), lambda: naina.Engine(tier)):
            try:
                return ctor()
            except TypeError:
                continue
        sys.exit("could not construct naina.Engine with a tier; check help(naina.Engine)")

    engine = make_engine()
    cold_ms = None
    per_page: list[dict] = []
    empties = 0

    for i, p in enumerate(paths, 1):
        img = np.asarray(Image.open(p).convert("RGB"), dtype=np.uint8)
        times = []
        md = ""
        for _ in range(max(1, a.runs)):
            t0 = time.perf_counter()
            md = engine.read(img).markdown
            times.append((time.perf_counter() - t0) * 1000)
        ms = sorted(times)[len(times) // 2]
        if i == 1:
            cold_ms = ms  # first call pays download and load
        else:
            per_page.append({"image": p.name, "ms": round(ms, 1), "chars": len(md),
                             "w": int(img.shape[1]), "h": int(img.shape[0])})
        if not md.strip():
            empties += 1
        (out / (p.stem + ".md")).write_text(md, encoding="utf-8")
        if not a.quiet:
            print(f"  {i:>4}/{len(paths)} {ms:7.0f} ms {len(md):6d} chars  {p.name}", flush=True)

    warm = sorted(r["ms"] for r in per_page)

    def pct(values: list[float], q: float) -> float | None:
        """Nearest-rank percentile. The naive int(n*q)-1 returns the SMALLEST
        element when n*q < 1, which made p95 come out below the median on a
        3-page smoke run (2026-09-22). Clamped, so a small sample reports the
        max rather than a number that is quietly wrong."""
        if not values:
            return None
        import math
        k = min(len(values), max(1, math.ceil(q * len(values))))
        return values[k - 1]

    summary = {
        "naina_version": naina.__version__,
        "tier": a.tier,
        "device": device_string(),
        "date": date.today().isoformat(),
        "pages": len(paths),
        "runs_per_page": a.runs,
        "cold_first_page_ms": round(cold_ms, 1) if cold_ms else None,
        "warm_median_ms": round(pct(warm, 0.5), 1) if warm else None,
        "warm_p95_ms": round(pct(warm, 0.95), 1) if warm else None,
        "warm_max_ms": round(warm[-1], 1) if warm else None,
        "empty_outputs": empties,
        "per_page": per_page,
    }
    (out / "_timing.json").write_text(json.dumps(summary, indent=1))

    print(f"\n{a.tier}: {len(paths)} pages | cold first {summary['cold_first_page_ms']} ms | "
          f"warm median {summary['warm_median_ms']} ms | p95 {summary['warm_p95_ms']} ms")
    if empties:
        print(f"WARNING: {empties} pages produced empty markdown", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
