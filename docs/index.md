# naina

Read documents. One C++ core, everywhere.

*naina* means **eyes** in Hindi.

[Try it in your browser →](https://jvoltci.github.io/naina/){ .md-button .md-button--primary }
[GitHub →](https://github.com/jvoltci/naina){ .md-button }

---

## What it does

Give naina a page. Get back the text, where each line sits, how confident it is,
and the document's structure as markdown.

```python
import naina

page = naina.read("invoice.png")
print(page.markdown)
```

```
# ACME Corporation

Invoice 2026-0417

| Item | Qty | Price |
...
```

## Why it exists

OCR accuracy is a commodity. PP-OCRv6's weights are Apache-2.0, so naina runs the
same models PaddleOCR runs and gets the same accuracy. Competing there is
pointless.

What no OCR library ships is **one engine that runs the same way everywhere**.
PaddleOCR needs Python and a 300 MB dependency tree. Browser libraries
reimplement the pipeline in TypeScript. Mobile gets a different SDK again. Each
of those is a separate implementation that can — and does — disagree with the
others.

naina is one C++ core behind a stable C ABI, with thin bindings on top:

| | |
|---|---|
| **Python** | pybind11 |
| **Node** | N-API |
| **Browser** | the same core compiled to WebAssembly |
| **Rust** | over the C ABI *(planned)* |
| **Flutter** | FFI to the C ABI *(planned)* |

The bindings contain no algorithmic logic. Detection post-processing, quad
rectification, CTC decoding, reading order and markdown assembly happen once, in
the core.

## What that buys you

**Python, Node and Rust produce byte-identical output** for a fixed backend,
device and tier. Not "similar" — identical.

The browser is close but not bit-exact, and that is stated plainly rather than
glossed: `onnxruntime-web` is a different build of ONNX Runtime with different
kernels, so probability maps differ in the last few float bits. See
[what it cannot do](limits.md).

## No heavy dependencies

Convex hull, minimum-area rectangle, polygon offsetting and contour tracing are
hand-written C++. There is **no OpenCV, no pyclipper, no PaddlePaddle**.

That is not purism. An 11 MB model tier behind a 300 MB dependency tree is not an
11 MB tier, and OpenCV compiled to WebAssembly would exceed the entire browser
budget on its own. naina's WASM binary is **143 KB brotli**.

## Model weights

PP-OCRv6 (detection, recognition) and PP-DocLayout (structure), all Apache-2.0.

Every file is mirrored into naina's own GitHub release and pinned by sha256, with
the upstream URL kept only as provenance. Upstream can rename, move or delete
anything and naina keeps working.

## Where to go next

- **[Install](install.md)** — pip, npm, or nothing at all in a browser
- **[Tiers](tiers.md)** — 11 MB, 54 MB or 269 MB, and how to pick
- **[What it cannot do](limits.md)** — read this before deploying anything
