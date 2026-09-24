# OmniDocBench

[OmniDocBench](https://github.com/opendatalab/OmniDocBench) v1.6: 1,651 real PDF
pages across nine document types, English and Chinese, scored on four
dimensions. It is the benchmark the current best open document readers are
ranked on, so it is the one that places naina honestly among them.

The scoring is the maintainers' own evaluator, not a reimplementation. naina's
side is only the prediction step.

## Run it

```bash
# 1. Fetch the pages (1.55 GB, research-only licence, never redistributed).
python3 benchmarks/omnidocbench/fetch.py --out build/omnidocbench/full

# 2. Install the evaluator once, in its own venv (it pins old deps and needs
#    Python 3.10 or 3.11).
git clone --depth 1 https://github.com/opendatalab/OmniDocBench.git build/omnidocbench/OmniDocBench
cd build/omnidocbench/OmniDocBench && python3 -m venv .venv && .venv/bin/pip install -e . && cd -

# 3. Predict, then score.
python3 benchmarks/omnidocbench/predict.py \
    --images build/omnidocbench/full/images --out build/omnidocbench/pred-tiny --tier tiny
python3 benchmarks/omnidocbench/score.py \
    --pred build/omnidocbench/pred-tiny \
    --gt build/omnidocbench/full/OmniDocBench.json --tier tiny
```

`score.py` writes a row to `results/` and prints it.

## Results

Edit distance is lower-is-better; TEDS is higher-is-better. Empty output scores
1.0 on edit distance and 0.0 on TEDS, which is what "does not attempt this"
looks like. Speed is a warm median with one Engine held for the whole run; the
first page, which pays model load, is reported separately.

**Full set, 1,651 pages, Apple M5 Pro. Tiny accuracy 2026-09-22 with speed re-measured 2026-09-23; small and medium 2026-09-23; see the correction below.**

| Tier | Text | Reading order | Formulas | Tables (TEDS) | Warm median | p95 |
|---|---|---|---|---|---|---|
| tiny | **0.248** | 0.492 | 0.799 | 0.000 | 367 ms | 2167 ms |
| small | **0.233** | 0.460 | 0.773 | 0.000 | 1188 ms | 6456 ms |
| medium | **0.184** | 0.442 | 0.794 | 0.000 | 2746 ms | 13177 ms |

Text by page language, full set, averaged per page and then across pages
(the evaluator's own convention; language from the ground truth's page
attribute; the evaluator writes its breakdown only for small runs):

| Language (pages) | tiny | small | medium |
|---|---|---|---|
| english (721) | 0.181 | 0.178 | 0.160 |
| simplified_chinese (710) | 0.259 | 0.230 | 0.169 |
| en_ch_mixed (112) | 0.293 | 0.247 | 0.219 |
| traditional_chinese (12) | 0.585 | 0.522 | 0.461 |
| other (2) | 0.060 | 0.114 | 0.089 |

**Corrected 2026-09-23.** An earlier version of this line averaged over matched
blocks instead of pages (tiny english 0.276, simplified_chinese 0.384). Block
averages weight dense pages, and because small matches more blocks (15,640
against 14,863) they made small look worse than tiny on English while it is
better by pages. Traditional Chinese at 0.52 to 0.59 on 12 pages remains the
script-coverage question for Task 1.

**Medium, and a defect it exposed.** Medium reads a page 7.5x slower than tiny
(2,746 ms median, p95 13.2 s, first page 3.1 s with model load) for a text
edit distance 26 percent lower, and it is the best tier on every language.
Five medium outputs are empty, all dense US newspaper pages (Wall Street
Journal pages 004 and 018, USA Today 012, Washington Post 042, Boston Globe
025) on which tiny and small produced 1,900 to 13,800 characters. Rerun alone
on the USA Today page (4,250 x 2,200): tiny detects 437 text boxes, medium
detects none. The medium detector under-fires on very small text after the
960-pixel resize; on the Globe and Mail page it found 84 boxes where tiny
found 536. Cause found the same day, by bisection: naina's own downscale samples the
source with plain bilinear interpolation and no antialiasing, and at a 4.4x
shrink the medium detector's probability map collapses (maximum 0.02) while
the tiny model's survives; with an area-averaged shrink the medium map is
normal and the same page pre-shrunk gives 367 boxes. The fix is a proper
downsampler in `core/src/image_ops.cc`, followed by a rerun of every tier,
because tiny's map improves too. Until then the medium row includes those
five pages as empties, scored as such.

Small reads a page 3.2x slower than tiny for a text edit distance 6 percent lower. Two small-tier pages produced empty output; both are near-blank pages (39 and 8 characters at tiny), not a tier failure. The small run's wall clock (01:33 to 07:47) is mostly the machine asleep; per-page timings are unaffected.

The full set is harder than the 18-page demo (tiny text 0.248 here against 0.185
there), which is why the demo table below is kept only as a record and never
quoted.

**18-page demo, same machine, 2026-09-22.** A smoke test, not a benchmark.

| Tier | Text | Reading order | Formulas | Tables (TEDS) | Warm median |
|---|---|---|---|---|---|
| tiny | 0.185 | 0.418 | 0.864 | 0.000 | 779 ms |
| medium | 0.111 | 0.384 | 0.804 | 0.000 | 17,424 ms, see below |

Demo text by language: tiny English 0.239, Chinese 0.125, mixed 0.343; medium
English 0.094, Chinese 0.055, mixed 0.146.

**Speed correction, 2026-09-23.** The tiny row's first speed figures (649 ms
median, 2,950 ms p95) were taken while the medium demo run, its weight
downloads and the evaluator shared the machine; file timestamps put the demo
run wholly inside the tiny run. Rerun alone on 2026-09-23 the predictions were
byte-identical (`diff -rq`, 0 files differ), so the accuracy columns did not
move, and the warm median fell to 367 ms, p95 to 2,167 ms, first page to
179 ms. Even that rerun shared the desktop with ordinary use, so every speed
here is an ordinary-desktop number, not lab-idle. Rule adopted: no speed is
published from a machine running any other job, and a full run's per-page
times are checked against an isolated warm read of the same page.

**The medium tier's cost, profiled 2026-09-23.** The demo table's medium
timing called `naina.read()`, which builds a throwaway Engine per call. With
one Engine held, on a quiet machine, medium reads the same pages 8.8x slower
than tiny: detection 8.5x, layout plus recognition 8.8x, recognition running
one line per call at about 0.8 ms a line at tiny and 8.4 ms at medium. That
is the compute of models 17 to 35x larger on the CPU, not a compile cost: the
first read of a new page equals a warm repeat within 30 ms. ONNX Runtime's
CoreML provider in its default format is slower than the CPU on every naina
graph (1.3x to 4x); in MLProgram format it is 1.5x faster for medium
detection and 1.9x for recognition, and cannot compile the medium layout
graph. Thread count is not a lever (best setting 1.10x over the default at
tiny, none at medium). The medium and small rows are being filled by the
same sequential rerun.

For scale, the reference PaddleOCR pipeline scores 0.071 on English text over
the full set. Naina's medium tier at 0.094 English on eighteen pages is in the
same class as the models it wraps; its tiny tier is not, and is not meant to
be.

## What these numbers mean

naina is a classical detect-then-recognise pipeline. It reads text lines and
assembles markdown. **It emits no table contents and does not attempt
formulas**, so those two columns are at their floor by construction, not by
failure, and together they are half of OmniDocBench's overall score. Anyone
comparing naina's overall number to a document VLM's is comparing two different
kinds of program. The text and reading-order columns are the honest comparison
today.

## Two things this harness was built to catch

**The formula metric changes silently.** OmniDocBench scores formulas with CDM,
which needs a LaTeX installation. Without one the evaluator falls back to edit
distance. `score.py` detects `pdflatex` and writes `formula_metric` into every
result row, so two runs scored differently can never be compared by accident.

**Percentiles lie on small samples.** The first version of `predict.py`
computed p95 as `sorted[int(n * 0.95) - 1]`, which returns the *smallest*
element when `n * 0.95 < 1`. A three-page run reported a p95 below its median.
It now uses a clamped nearest-rank percentile, and the ordering is asserted
across sample sizes.
