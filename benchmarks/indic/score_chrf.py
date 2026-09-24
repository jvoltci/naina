"""Score a prediction list against gt.json the way the Devanagari paper does, so the row
is comparable to its table: NFC both sides, code-point CER per sample via jiwer, mean and
median CER, catastrophic rate (share of samples with CER above 50 percent, the paper's
report_real.py threshold), WER, and chrF++ (sacrebleu corpus_chrf with word_order=2).

Run with the benchmark venv, not the system Python:
    build/indic/.venv/bin/python benchmarks/indic/score_chrf.py --gt ... --pred ... --label tiny
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import unicodedata
from datetime import date
from pathlib import Path

import jiwer
import sacrebleu
from importlib.metadata import version as pkg_version

HERE = Path(__file__).resolve().parent


def nfc(s: str) -> str:
    return unicodedata.normalize("NFC", s).strip()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gt", required=True)
    ap.add_argument("--pred", required=True)
    ap.add_argument("--label", required=True, help="row name, e.g. tiny or small_pad0.5")
    a = ap.parse_args()

    refs = [nfc(x) for x in json.loads(Path(a.gt).read_text(encoding="utf-8"))]
    hyps = [nfc(x) for x in json.loads(Path(a.pred).read_text(encoding="utf-8"))]
    if len(refs) != len(hyps):
        sys.exit(f"{len(refs)} references but {len(hyps)} predictions; not scoring a misaligned pair")
    if not refs:
        sys.exit("empty ground truth")

    cers = [jiwer.cer(r, h) * 100 if h else 100.0 for r, h in zip(refs, hyps)]
    wers = [jiwer.wer(r, h) * 100 if h else 100.0 for r, h in zip(refs, hyps)]
    row = {
        "label": a.label, "n": len(refs), "date": date.today().isoformat(),
        "chrF++": round(sacrebleu.corpus_chrf(hyps, [refs], word_order=2).score, 2),
        "mean_CER": round(statistics.mean(cers), 2),
        "median_CER": round(statistics.median(cers), 2),
        "catastrophic_pct_CER_gt_50": round(100 * sum(c > 50 for c in cers) / len(cers), 1),
        "mean_WER": round(statistics.mean(wers), 2),
        "empty_predictions": sum(1 for h in hyps if not h),
        "mean_len_ratio": round(statistics.mean(len(h) / max(1, len(r)) for r, h in zip(refs, hyps)), 2),
        "metric_impl": f"sacrebleu {pkg_version('sacrebleu')} chrF++ word_order=2; jiwer {pkg_version('jiwer')} code-point CER",
    }
    dest = HERE / "results"
    dest.mkdir(exist_ok=True)
    (dest / f"deva300-{a.label}-{row['date']}.json").write_text(json.dumps(row, indent=1))
    for k, v in row.items():
        print(f"  {k}: {v}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
