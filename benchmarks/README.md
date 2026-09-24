# naina benchmarks

Every number naina publishes is produced here, and every one of them says which
tier, which device and which date. The rule from the main README holds: measured
numbers with the harness in the repository, or nothing.

| Suite | What it measures | Where |
|---|---|---|
| [`omnidocbench/`](omnidocbench/) | text, tables, formulas and reading order on 1,651 real PDF pages | OmniDocBench v1.6, run with the maintainers' own evaluator |
| `indic/` | Devanagari, Tamil, Telugu and the other scripts naina ships | word crops, synthetic renders and real photographed pages |

Speed, memory and energy per page are recorded alongside accuracy by the same
harness, because a reader that is right and too slow to use is not finished.

> **What used to be here.** A face-detection latency template (`latency.py`,
> `runner.py`) that measured nothing naina does. It came from the project
> scaffold and was removed on 2026-09-22, along with the one result file it had
> ever produced.
