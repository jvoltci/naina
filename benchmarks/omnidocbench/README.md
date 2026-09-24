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

**Full set, 1,651 pages, Apple M5 Pro, macOS 27.0, CPU execution provider,
naina 0.2.1 at ab01c40. Accuracy 2026-09-24 (tiny, small) and 2026-09-25
(medium); speed from the same runs, see the note under the table.**

| Tier | Text | Reading order | Formulas | Tables (TEDS) | Empty pages | First page | Warm median | p95 |
|---|---|---|---|---|---|---|---|---|
| tiny | **0.248** | 0.492 | 0.799 | 0.000 | 0 | 140 ms | 154 ms | 711 ms |
| small | **0.233** | 0.460 | 0.773 | 0.000 | 2 | 529 ms | 578 ms | 2,934 ms |
| medium | **0.173** | 0.434 | 0.769 | 0.000 | 0 | 1,933 ms | 2,066 ms | 11,208 ms |

Rows: `results/tiny-1651p-2026-09-24-cpu.json`,
`results/small-1651p-2026-09-24-cpu.json`,
`results/medium-1651p-2026-09-25-cpu.json`. Each row records its provider
and what else the machine was doing.

Text by page language, full set, averaged per page and then across pages
(the evaluator's own convention; language from the ground truth's page
attribute):

| Language (pages) | tiny | small | medium |
|---|---|---|---|
| english (721) | 0.181 | 0.178 | 0.145 |
| simplified_chinese (710) | 0.259 | 0.230 | 0.156 |
| en_ch_mixed (112) | 0.293 | 0.247 | 0.213 |
| traditional_chinese (12) | 0.585 | 0.522 | 0.520 |
| other (2) | 0.060 | 0.114 | 0.088 |

**Corrected 2026-09-23.** An earlier version of this line averaged over matched
blocks instead of pages (tiny english 0.276, simplified_chinese 0.384). Block
averages weight dense pages, and because small matches more blocks (15,640
against 14,863) they made small look worse than tiny on English while it is
better by pages. Traditional Chinese at 0.52 to 0.59 on 12 pages remains the
script-coverage question for Task 1.

**What the tiers buy.** Small reads a page 3.7x slower than tiny
for a text edit distance 6 percent lower. Medium reads a page 13.4x
slower than tiny and 3.6x slower than small for a text edit distance 30
percent lower than tiny and 26 percent lower than small, and it is the best
tier on every language. The two small-tier empties are near-blank pages (39
and 8 characters at tiny), not a tier failure.

**The execution provider, 2026-09-24.** Until commit ab01c40 naina appended
ONNX Runtime's CoreML provider by default. On 2026-09-23 it measured slower
than the CPU on every naina graph (1.3x to 4x in its default format).
Overnight into 2026-09-24 the Mac went from macOS 26.5 to 27.0 and, with
identical code and weights, tiny's outputs changed on 1,595 of 1,651 pages
(text 0.248 to 0.309) and small's on 1,470. CPU-only runs under macOS 27
reproduced the 26.5 rows: tiny byte-identical on 1,651 of 1,651 pages, small
on 1,650 of 1,651, every score equal to four decimals. So the earlier rows
were CPU rows in effect, and the CPU is now the default provider;
`NAINA_DEVICE=auto|cpu|gpu|npu` overrides it. The CoreML rows are kept as
`results/*-coreml.json` for the record and are not comparable with the table
above. Under CoreML medium scored 0.1731 against 0.1728 here while 618 pages
differed in content: the provider moved medium's outputs without moving its
aggregate. Rows compare only inside one provider and one OS.

**Medium, the defect it exposed, and the fix.** On 2026-09-23 five medium
outputs were empty, all dense US newspaper pages (Wall Street Journal pages
004 and 018, USA Today 012, Washington Post 042, Boston Globe 025) on which
tiny and small produced 1,900 to 13,800 characters. Rerun alone on the USA
Today page (4,250 x 2,200): tiny detected 437 text boxes, medium none. Cause,
by bisection the same day: naina's own downscale sampled the source with
plain bilinear interpolation and no antialiasing, and at a 4.4x shrink the
medium detector's probability map collapsed (maximum 0.02) while the tiny
model's survived; the same page pre-shrunk with an area average gave 367
boxes. The fix is a separable resampler in `core/src/image_ops.cc` with the
filter chosen per tier on the full set and pinned in the registry: tiny
bilinear, small bilinear, medium triangle. Lanczos3 won a 30-page subset for
tiny and lost the full set (0.3115 against 0.2477), so a subset picks a
candidate and never a winner. Judged in one environment, old code against
new code, both macOS 27 under the CoreML provider then in use: medium 0.1839
to 0.1731 text, 5 empty pages to 0, every document type improved or held.
The medium row above is the new code on the CPU. The evidence and the reject
table are in the lab's private note on the detector resampler (2026-09-23).

**Speed, and what the machine was doing.** Every speed above is from the same
run as its accuracy row, on mains power, with the prediction process alone on
the machine apart from an editor and an idle browser: an ordinary desktop,
not a lab. Small's first ten minutes shared the machine with a wheel build
and a core rebuild; the median is robust to that, the p95 less so. Tiny's
speed is from a rerun on a quiet machine after the others finished, whose
1,651 outputs were byte-identical to its accuracy run. As the check this
README promised on 2026-09-23, 50 pages spread across the set were then read
warm three times each, in isolation: tiny 172 ms against 154 ms over the full run, small 599 against 578,
medium 2,112 against 2,066, each within 11 percent, the sample being a
little heavier in newspapers than the set. Rule kept: no speed is
published from a machine running another job or on battery.

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
179 ms. Those figures were taken under the CoreML provider on macOS 26.5 and
are superseded by the table above. Rule adopted that day: no speed is
published from a machine running any other job, and a full run's per-page
times are checked against an isolated warm read.

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
tiny, none at medium).

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
