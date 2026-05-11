#!/usr/bin/env python3
"""Aggregate latency JSONs into the README benchmark table.

    python benchmarks/runner.py --emit-markdown > /tmp/table.md

The table is also written back into README.md between the
`<!-- BENCH:LATENCY -->` markers when --update-readme is passed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Row:
    target: str
    tier: str
    detect_p50: float | None
    detect_p95: float | None
    embed_p50: float | None
    embed_p95: float | None
    system: str
    naina_version: str


def load_results(results_dir: Path) -> list[Row]:
    rows: list[Row] = []
    for path in sorted(results_dir.glob("*.json")):
        record = json.loads(path.read_text())
        detect = record.get("detect_ms") or {}
        embed = record.get("embed_ms") or {}
        sysd = record.get("system", {})
        rows.append(
            Row(
                target=record.get("target", path.stem),
                tier=record.get("tier", "default"),
                detect_p50=detect.get("p50"),
                detect_p95=detect.get("p95"),
                embed_p50=embed.get("p50"),
                embed_p95=embed.get("p95"),
                system=f'{sysd.get("system","?")} {sysd.get("machine","?")}',
                naina_version=record.get("naina_version", ""),
            )
        )
    return rows


def emit_markdown(rows: list[Row]) -> str:
    if not rows:
        return "_No benchmark results yet. Run `python benchmarks/latency.py --target …`._\n"

    head = (
        "| Target | Tier | Host | Detect p50 | Detect p95 | Embed p50 | Embed p95 |\n"
        "|---|---|---|---|---|---|---|\n"
    )

    def fmt(v: float | None) -> str:
        return f"{v:.1f} ms" if v is not None else "—"

    body = "".join(
        f"| {r.target} | {r.tier} | {r.system} | "
        f"{fmt(r.detect_p50)} | {fmt(r.detect_p95)} | "
        f"{fmt(r.embed_p50)} | {fmt(r.embed_p95)} |\n"
        for r in rows
    )
    return head + body


def update_readme(table_md: str, readme: Path) -> None:
    """Replace content between <!-- BENCH:LATENCY --> markers."""
    text = readme.read_text()
    pattern = re.compile(
        r"<!-- BENCH:LATENCY -->.*?<!-- /BENCH:LATENCY -->",
        re.DOTALL,
    )
    block = f"<!-- BENCH:LATENCY -->\n\n{table_md}\n<!-- /BENCH:LATENCY -->"
    if pattern.search(text):
        new_text = pattern.sub(block, text)
    else:
        # No markers yet — append a new section to the end.
        new_text = text.rstrip() + "\n\n## Benchmarks (auto-generated)\n\n" + block + "\n"
    readme.write_text(new_text)


def main() -> int:
    ap = argparse.ArgumentParser(description="naina benchmark aggregator")
    ap.add_argument("--results-dir", type=Path, default=Path("benchmarks/results"))
    ap.add_argument("--emit-markdown", action="store_true",
                    help="print the latency table to stdout")
    ap.add_argument("--update-readme", action="store_true",
                    help="splice the table into README.md between BENCH:LATENCY markers")
    ap.add_argument("--readme", type=Path, default=Path("README.md"))
    args = ap.parse_args()

    if not args.results_dir.exists():
        print(f"no results dir at {args.results_dir}", file=sys.stderr)
        return 1

    rows = load_results(args.results_dir)
    table = emit_markdown(rows)

    if args.emit_markdown or not args.update_readme:
        sys.stdout.write(table)
    if args.update_readme:
        update_readme(table, args.readme)
        print(f"updated {args.readme}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
