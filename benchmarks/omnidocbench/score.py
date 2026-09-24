"""Score a folder of naina predictions with OmniDocBench's own evaluator.

Writes the evaluator's config, runs it in the evaluator's venv, and pulls the
four dimension numbers out of its result JSON into one row.

Formulas: the benchmark's CDM metric needs LaTeX. When `pdflatex` is absent the
config asks for edit distance only and every row says so, rather than silently
reporting a different metric under the same name.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from datetime import date
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
EVAL = REPO / "build/omnidocbench/OmniDocBench"


def build_config(pred_dir: Path, gt_json: Path, workers: int, have_latex: bool) -> Path:
    formula_metrics = "      - Edit_dist\n" + ("      - CDM\n" if have_latex else "")
    text = f"""end2end_eval:
  metrics:
    text_block:
      metric:
      - Edit_dist
    display_formula:
      metric:
{formula_metrics}      cdm_workers: {workers}
    table:
      metric:
      - TEDS
      - Edit_dist
      teds_workers: {workers}
    reading_order:
      metric:
      - Edit_dist
  dataset:
    dataset_name: end2end_dataset
    ground_truth:
      data_path: {gt_json}
    prediction:
      data_path: {pred_dir}
    match_method: quick_match
    match_workers: {workers}
    quick_match_truncated_timeout_sec: 300
    match_timeout_sec: 420
    timeout_fallback_max_chunk_span: 10
    timeout_fallback_order_penalty: 0.10
"""
    cfg = EVAL / "configs" / f"naina-{pred_dir.name}.yaml"
    cfg.write_text(text)
    return cfg


def numbers(result: Path, gt_path: Path) -> dict:
    d = json.loads(result.read_text())
    out: dict[str, object] = {}
    for dim in ("text_block", "display_formula", "table", "reading_order"):
        a = d.get(dim, {}).get("all", {})
        ed = a.get("Edit_dist", {})
        if isinstance(ed, dict):
            out[f"{dim}_edit"] = round(ed.get("ALL_page_avg", float("nan")), 4)
        teds = a.get("TEDS", {})
        if isinstance(teds, dict) and "all" in teds:
            out["table_TEDS"] = round(teds["all"], 4)
        cdm = a.get("CDM", {})
        if isinstance(cdm, dict) and cdm:
            out["formula_CDM"] = round(list(cdm.values())[0], 4)
    lang = d.get("text_block", {}).get("group", {}).get("Edit_dist", {})
    for k, v in (lang or {}).items():
        if isinstance(v, (int, float)) and "text_language" in k:
            out["text_" + k.split(":")[-1].strip()] = round(v, 4)
    if not any(k.startswith("text_text_") for k in out):
        # The evaluator writes the attribute breakdown only for small runs
        # (present on 18 pages, absent on 1,651, seen 2026-09-23). Compute it
        # from its per-block output. Language comes from the ground truth's
        # page attribute, joined on image id (the per-block gt_attribute is
        # empty on the full set). Averaged PER PAGE, then across pages, which
        # is the evaluator's own convention: a block-level average weights
        # dense pages and inverted the tier ranking on 2026-09-23 (small looked
        # worse than tiny on English by blocks, better by pages).
        blocks = result.with_name(result.name.replace("metric_result", "text_block_result"))
        if blocks.exists():
            import statistics
            page_lang: dict[str, str] = {}
            try:
                for page in json.loads(gt_path.read_text()):
                    info = page.get("page_info", {})
                    page_lang[info.get("image_path", "")] = (info.get("page_attribute") or {}).get("language", "unknown")
            except Exception:  # noqa: BLE001
                pass
            per_page: dict[str, list[float]] = {}
            for smp in json.loads(blocks.read_text()):
                per_page.setdefault(smp.get("img_id", ""), []).append(smp["edit"])
            by_lang: dict[str, list[float]] = {}
            for img, edits in per_page.items():
                by_lang.setdefault(page_lang.get(img, "unknown"), []).append(statistics.mean(edits))
            for lang_name, vals in by_lang.items():
                out["text_" + lang_name + "_page_avg"] = round(statistics.mean(vals), 4)
                out["text_" + lang_name + "_pages"] = len(vals)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--pred", required=True, help="folder of .md predictions")
    ap.add_argument("--gt", required=True, help="OmniDocBench ground-truth json")
    ap.add_argument("--tier", default="")
    ap.add_argument("--workers", type=int, default=4)
    a = ap.parse_args()

    if not EVAL.exists():
        sys.exit(f"evaluator not found at {EVAL}. Clone opendatalab/OmniDocBench there and "
                 f"install it in its own venv (see README).")
    venv_py = EVAL / ".venv/bin/python"
    if not venv_py.exists():
        sys.exit(f"evaluator venv not found at {venv_py}")

    pred = Path(a.pred).resolve()
    gt = Path(a.gt).resolve()
    have_latex = shutil.which("pdflatex") is not None
    cfg = build_config(pred, gt, a.workers, have_latex)
    print(f"scoring {pred.name} against {gt.name} | formula metric: "
          f"{'edit + CDM' if have_latex else 'edit only, no LaTeX installed'}")

    r = subprocess.run([str(venv_py), "pdf_validation.py", "--config", str(cfg.relative_to(EVAL))],
                       cwd=EVAL, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-3000:]); print(r.stderr[-3000:], file=sys.stderr)
        sys.exit(f"evaluator failed with {r.returncode}")

    result = EVAL / "result" / f"{pred.name}_quick_match_metric_result.json"
    if not result.exists():
        sys.exit(f"evaluator produced no {result}")
    row = numbers(result, gt)

    timing = {}
    tj = pred / "_timing.json"
    if tj.exists():
        t = json.loads(tj.read_text())
        timing = {k: t.get(k) for k in ("naina_version", "tier", "device", "pages",
                                        "cold_first_page_ms", "warm_median_ms", "warm_p95_ms",
                                        "empty_outputs")}

    out = {"date": date.today().isoformat(), "tier": a.tier or timing.get("tier", "?"),
           "formula_metric": "edit+CDM" if have_latex else "edit only",
           **timing, **row}
    dest = HERE / "results"
    dest.mkdir(exist_ok=True)
    name = f"{out['tier']}-{out.get('pages','?')}p-{out['date']}.json"
    (dest / name).write_text(json.dumps(out, indent=1))

    print()
    for k, v in out.items():
        print(f"  {k}: {v}")
    print(f"\nwrote {dest / name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
