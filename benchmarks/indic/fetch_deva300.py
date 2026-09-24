"""Build the 300-crop real printed-Devanagari set that Aditya-PS-05/devanagari-ocr-benchmark
scores on, so naina's number sits in the same table as its published rows.

Reproduces that repository's scripts/make_real.py (MIT): the first N rows of the
Process-Venue/Sanskrit-OCR-Typed-Dataset train split whose NFC-normalised label has
3 or more characters and at least one Devanagari code point. There is no seed; the split
is iterated in stored order. The parquet is read directly, so the `datasets` package is
not needed.

These are word and short-phrase crops (median 87 x 30 px, one word), not pages. The Hub
states the licence twice and the two disagree (metadata cc-by-nc-sa-4.0, card body MIT);
the crops are a benchmark input here and are never redistributed.
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import unicodedata
import urllib.request
from datetime import date
from pathlib import Path

import pyarrow.parquet as pq
from PIL import Image

PARQUET_URL = ("https://huggingface.co/datasets/Process-Venue/Sanskrit-OCR-Typed-Dataset/"
               "resolve/main/data/train-00000-of-00001.parquet")
EXPECTED_ROWS = 2765  # seen 2026-09-23; a different count means the set moved under us


def nfc(s: str) -> str:
    return unicodedata.normalize("NFC", s.strip())


def is_deva(s: str) -> bool:
    return any("ऀ" <= c <= "ॿ" for c in s)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="build/indic/deva300")
    ap.add_argument("--n", type=int, default=300)
    a = ap.parse_args()

    out = Path(a.out)
    (out / "images").mkdir(parents=True, exist_ok=True)
    parquet = out / "train.parquet"
    if not parquet.exists():
        print(f"downloading {PARQUET_URL}", flush=True)
        urllib.request.urlretrieve(PARQUET_URL, parquet)
    table = pq.read_table(parquet)
    if table.num_rows != EXPECTED_ROWS:
        sys.exit(f"train split has {table.num_rows} rows, expected {EXPECTED_ROWS}; "
                 f"the dataset changed and the 300 are no longer the paper's 300")

    labels: list[str] = []
    rows_used: list[int] = []
    df = table.to_pandas()
    for i, row in df.iterrows():
        lab = nfc(row["label"])
        if len(lab) < 3 or not is_deva(lab):
            continue
        img_field = row["image"]
        raw = img_field["bytes"] if isinstance(img_field, dict) else img_field
        img = Image.open(io.BytesIO(raw)).convert("RGB")
        if img.width < 8 or img.height < 8:
            sys.exit(f"row {i}: image {img.size} is too small to be a crop; refusing to continue")
        img.save(out / "images" / f"{len(labels):04d}.png")
        labels.append(lab)
        rows_used.append(int(i))
        if len(labels) >= a.n:
            break

    if len(labels) < a.n:
        sys.exit(f"only {len(labels)} labels passed the filter, wanted {a.n}")

    (out / "gt.json").write_text(json.dumps(labels, ensure_ascii=False, indent=0), encoding="utf-8")
    (out / "manifest.json").write_text(json.dumps({
        "source": PARQUET_URL, "split_rows": table.num_rows, "n": len(labels),
        "last_source_row": rows_used[-1], "rows_used": rows_used,
        "selection": "first n rows in stored order with NFC label length >= 3 and any Devanagari code point",
        "reproduces": "Aditya-PS-05/devanagari-ocr-benchmark scripts/make_real.py",
        "licence_note": "Hub metadata cc-by-nc-sa-4.0, card body MIT; benchmark input only",
        "date": date.today().isoformat(),
    }, indent=1))
    print(f"wrote {len(labels)} crops and gt.json to {out} (source rows 0..{rows_used[-1]}, "
          f"{rows_used[-1] + 1 - len(labels)} rows dropped by the filter)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
