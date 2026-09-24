"""Fetch OmniDocBench (1,651 pages) with parallel workers and resume.

The sequential fetch measured 2026-09-22 ran at about 90 images per ten
minutes, three hours for the set. Eight workers bring it under thirty.

Licence: the dataset is research-only. It is downloaded for measurement and
never redistributed from this repository.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

REPO = "opendatalab/OmniDocBench"
BASE = f"https://huggingface.co/datasets/{REPO}/resolve/main"
API = f"https://huggingface.co/api/datasets/{REPO}?blobs=true"


def listing() -> list[tuple[str, int]]:
    with urllib.request.urlopen(API, timeout=60) as r:
        data = json.load(r)
    out = []
    for s in data["siblings"]:
        name = s["rfilename"]
        if name == "OmniDocBench.json" or name.startswith("images/"):
            out.append((name, s.get("size") or 0))
    return out


def fetch_one(name: str, size: int, root: Path, retries: int = 3) -> tuple[str, str]:
    dst = root / name
    if dst.exists() and (size == 0 or dst.stat().st_size == size):
        return name, "cached"
    dst.parent.mkdir(parents=True, exist_ok=True)
    part = dst.with_suffix(dst.suffix + ".part")
    # Percent-encode the path. 161 of the 1,651 filenames contain spaces
    # ("PPT_13 fallacies LALT2012_page_001.png") and an unencoded space is a
    # 404, not a network error. Found 2026-09-22 after two full passes.
    url = f"{BASE}/{urllib.parse.quote(name)}"
    for attempt in range(retries):
        try:
            urllib.request.urlretrieve(url, part)
            part.replace(dst)
            return name, "ok"
        except Exception as exc:  # noqa: BLE001
            if attempt == retries - 1:
                # Name the file. The first version printed only the exception,
                # so 161 consistent 404s were indistinguishable from each other
                # and from a network problem.
                return name, f"FAILED {name}: {exc}"
    return name, f"FAILED {name}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/omnidocbench/full")
    ap.add_argument("--workers", type=int, default=8)
    a = ap.parse_args()
    root = Path(a.out)
    files = listing()
    total = sum(s for _, s in files)
    print(f"{len(files)} files, {total/1e9:.2f} GB, {a.workers} workers, into {root}")
    ok = cached = failed = 0
    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        futures = {pool.submit(fetch_one, n, s, root): n for n, s in files}
        for i, fut in enumerate(as_completed(futures), 1):
            _, status = fut.result()
            if status == "ok":
                ok += 1
            elif status == "cached":
                cached += 1
            else:
                failed += 1
                print("  " + status, file=sys.stderr)
            if i % 100 == 0 or i == len(files):
                print(f"  {i}/{len(files)}  new {ok}  cached {cached}  failed {failed}", flush=True)
    print(f"done: {ok} fetched, {cached} already present, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
