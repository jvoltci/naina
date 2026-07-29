# What it cannot do

Read this before you deploy naina anywhere that matters. Everything here is
measured, and several of these were found the hard way.

## It returns confident nonsense on scripts it cannot read

PP-OCRv6's character set covers Latin and CJK. It has **no Devanagari** — and
nothing in the output says so.

A Devanagari page returns lines like `3rarearanlus Tarafaaa: f:` at **0.758
confidence**. CTC confidence measures how sure the model is about the path it
chose through its own alphabet; it cannot express "these glyphs are not in my
alphabet at all". So the number is high and meaningless.

!!! danger "There is no automatic guard for this yet"
    If you might receive documents in an unsupported script, check the language
    before calling naina. Do not use confidence as a proxy — it will not save you.

Tracked in the [roadmap](ROADMAP.md).

## Handwriting is weak

PP-OCRv6 is trained on print. Claiming handwriting support would be dishonest.

## Layout degrades outside its training distribution

PP-DocLayout is trained on papers and reports and is excellent on them — 14 of 14
regions correctly labelled on an A4 academic page. On an unusual layout it can
mislabel a body paragraph as a title, which puts a whole paragraph under a `##`
heading in the markdown.

Text extraction is much more robust than structure. If you only need text, use
`page.lines` and ignore the markdown.

## One box can come back with two labels

PaddleDetection runs NMS per class, so the same region can be returned under
several labels and naina currently keeps every one above threshold. Measured: a
running head returned `text` at 0.677 *and* `header` at 0.481 for the identical
box.

When two labels for one box both clear the threshold, you get two regions. A
cross-class dedup pass is on the roadmap.

## Tables are detected, not parsed

A table region is located and labelled. Its cell structure is not extracted. The
markdown marks it explicitly rather than inventing a grid:

```
[table: structure not parsed]
```

That is deliberate. A plausible-looking wrong table is worse than an honest gap.

## Chart and formula contents are not interpreted

Same rule: regions get found and labelled, contents are not read.

## The browser is close to native, not bit-identical

naina guarantees **byte-identical output across bindings for one backend build** —
Python, Node and Rust run the same core against the same kernels.

WebAssembly is outside that guarantee, and this is measured rather than assumed.
`onnxruntime-web` is a different build of ONNX Runtime, using WASM SIMD kernels
instead of native NEON/AVX, so probability maps differ in the last few float bits.
On an A4 page at `tiny`:

| | Native macOS arm64 | Browser (WASM) |
|---|---|---|
| Text lines | 35 | 33 |
| Character-identical | — | 33 |

One marginal blob landed on the other side of DBNet's 0.3 binarize threshold,
which changed line segmentation. Because a split fragment takes its own
reading-order slot, **word order can shift with it**.

What the browser does guarantee: determinism within itself (same input, same
output) and the same algorithms, since it runs the same C++.

## WebGPU is off by default because it silently breaks layout

Chrome 141 on an M3 initialises ONNX Runtime's JSEP (WebGPU) provider and then
fails a kernel:

```
[E:onnxruntime] Non-zero status code returned while running MatMul node.
Name:'MatMul.3' Status Message: Failed to run JSEP kernel
```

ORT recovers node by node, so text recognition still returned 33 lines at 0.99
confidence and the result *looked* correct. But layout detection returned **0
regions instead of 9**, and the markdown lost all its structure.

A silent quality regression is worse than a crash. naina defaults to `['wasm']`;
WebGPU is opt-in until there are real numbers.

## Not in scope, on purpose

- **Training or fine-tuning.** naina is inference only.
- **Autoregressive VLM parsing** (PaddleOCR-VL, DeepSeek-OCR). These need a
  tokenizer, KV cache and sampling loop — a different engine, not a module.
- **Face and person understanding.** naina v0.1 was this. It is preserved on the
  [`face-stack`](https://github.com/jvoltci/naina/tree/face-stack) branch.
- Vector stores, dashboards, UI frameworks.
- Crime prediction, risk scoring, government-ID matching.

## Known build traps

**Backends default to OFF.** `NAINA_WITH_ONNXRUNTIME=OFF` produces a library with
no inference backend, and the suite still reports 100% green because every test
needing a backend *skips*. Set `NAINA_REQUIRE_BACKEND=1` to make those skips
fail.

**`FindNCNN.cmake` does not locate a Homebrew NCNN install**, so in practice only
the ONNX Runtime backend is exercised.
