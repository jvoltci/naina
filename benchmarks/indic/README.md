# Indic scripts

Three sets, each a different kind of evidence. Nothing here is a page-level
public benchmark, because none exists for Indic scripts; the third set is the
one this repository makes itself.

| Set | What it is | Status |
|---|---|---|
| Devanagari 300 | 300 real printed word and short-phrase crops (median 87 x 30 px) from the Sanskrit-OCR-Typed dataset, selected exactly as the devanagari-ocr-benchmark paper selects them | built, `fetch_deva300.py` |
| GlotOCR-bench | synthetic renders across scripts; `cis-lmu/GlotOCR-bench` is gated and needs a Hugging Face access request | waiting on access |
| Antaryaatra pages | 50 photographed pages of printed editions of the texts in `they/`, ground truth from the Unicode source, CC-BY-4.0 | not started; needs the photographs |

## Run the Devanagari 300

```bash
# 1. Crops and ground truth (7.8 MB parquet, read directly; no `datasets` needed)
python3 benchmarks/indic/fetch_deva300.py --out build/indic/deva300

# 2. Scoring libraries in their own venv, never in the system Python
python3 -m venv build/indic/.venv && build/indic/.venv/bin/pip install sacrebleu jiwer

# 3. Predict, raw crop and with a white margin of half the crop height, then score
NAINA_OFFLINE=1 python3 benchmarks/indic/predict_deva.py --tier tiny
NAINA_OFFLINE=1 python3 benchmarks/indic/predict_deva.py --tier tiny --pad 0.5
build/indic/.venv/bin/python benchmarks/indic/score_chrf.py \
    --gt build/indic/deva300/gt.json --pred build/indic/deva300/pred_tiny.json --label tiny
```

`score_chrf.py` writes a row to `results/` and prints it.

## Results, Devanagari 300

Apple M5 Pro, macOS 27.0, CPU execution provider, naina 0.2.1 at ab01c40,
alphabet `devanagari`, 2026-09-25; the tiny and small predictions are
byte-identical to the first run of 2026-09-23. chrF++ higher
is better; CER lower is better; "empty" is crops where detection found no
line, scored as CER 100. Raw is the crop as the paper's runners see it; pad
0.5 pastes the crop on white with a margin of half its height.

| Tier, input | chrF++ | mean CER | median CER | CER over 50 percent | empty | ms per crop |
|---|---|---|---|---|---|---|
| **small, raw** | **76.6** | 6.1 | 0.0 | 0.3 | 0 | 58 |
| small, pad 0.5 | 74.5 | 6.9 | 0.0 | 0.0 | 0 | 57 |
| medium, pad 0.5 | 73.5 | 7.1 | 0.0 | 0.0 | 0 | 183 |
| tiny, pad 0.5 | 71.8 | 8.4 | 0.0 | 1.0 | 1 | 23 |
| medium, raw | 51.1 | 48.0 | 18.9 | 44.0 | 131 | 181 |
| tiny, raw | 6.9 | 91.8 | 100.0 | 91.7 | 258 | 12 |

Where that sits on the paper's own table (chrF++, same 300 crops): Gemini
2.5 Flash 86.3, Claude Opus 4.7 82.2, **naina small 76.6**, Qwen3-VL-8B 75.2,
EasyOCR 58.3, DeepSeek-OCR 10.4. A 54 MB Apache-2.0 pipeline on a laptop CPU,
at 58 ms a crop, reads real printed Devanagari better than an 8-billion
parameter vision-language model and worse than the two closed frontier
models. Numbers recomputed a second time from the prediction files with
sacrebleu and jiwer directly; they match to the decimal.

**Read the table with these three facts.** Every tier uses the same
recogniser, so the spread is detection: tiny and medium both lose most crops
raw and recover with a margin (tiny 6.9 to 71.8, medium 51.1 to 73.5), small
finds the line either way and loses a point from the margin. Medium's raw
failure is the same small-text detection weakness seen on the OmniDocBench
newspapers. The per-crop times are warm medians of one pass, taken with the
prediction alone on the machine on mains power in the minutes after a scoring
run had finished, so they are indicative, not the three-run medians the rest
of this repository publishes.

The paper's published rows on the same 300 crops, for scale (chrF++, higher is
better; from the README of `Aditya-PS-05/devanagari-ocr-benchmark`, read
2026-09-23, closed-model numbers reflect API versions at their eval time):
Gemini 2.5 Flash 86.3, Claude Opus 4.7 82.2, Qwen3-VL-8B 75.2, EasyOCR 58.3,
DeepSeek-OCR 10.4.

**Corrected 2026-09-25.** The Mac took a macOS update overnight into
2026-09-24 (26.5.2 to 27.0) and naina's outputs moved with no code change,
because ONNX Runtime's CoreML provider, appended by default until commit
ab01c40, changed its arithmetic with the OS
(the lab's private note on the detector resampler (2026-09-23)). The CPU is now the
default provider, and every row above is a CPU run under macOS 27. Tiny and
small are byte-identical to the 2026-09-23 predictions, so those rows did not
move; the 76.39 this paragraph quoted for small on 2026-09-24 was the CoreML
provider under macOS 27, not the model. Medium's rows moved for one reason,
the detector filter changing from bilinear to triangle in the resampler fix:
raw 49.8 to 51.1 chrF++ with four fewer empties, padded 74.0 to 73.5. The
same two numbers come out under CoreML on macOS 27 (51.11 and 73.52), so on
these crops the provider is not a factor. Rows:
`results/deva300-cpu27_*-2026-09-25.json`; the 2026-09-23 and CoreML rows
stay beside them for the record.

## What these numbers mean

**The crops are words, not pages.** The paper says so itself and flags that the
format disadvantages page-oriented systems. naina is one: it detects lines,
then recognises them, and on a bare 30-pixel-high word its detector sometimes
finds no line at all, which scores as an empty prediction (CER 100). The padded
row gives every crop a margin so the detector sees a line; both rows are
published because neither alone is the truth.

**Every tier reads Devanagari with the same recogniser.** The registry ships
one Devanagari model (PP-OCRv5 mobile, Apache-2.0), and the medium tier
resolves to it. Tiers differ only in detection and layout here, so the spread
between tiers on this set is a detection spread, and the recogniser is the
ceiling for any Indic claim below the `large` tier.

**Metrics match the paper's script.** NFC normalisation on both sides,
code-point CER via jiwer (not grapheme clusters), mean and median CER, the share
of samples with CER above 50 percent, and chrF++ as sacrebleu's corpus chrF with
word order 2. The dataset's licence is stated twice on the Hub and the two
disagree (metadata cc-by-nc-sa-4.0, card body MIT); the crops are a benchmark
input here and are never redistributed.
