# Roadmap

Versioned, contributor-facing. naina ships **vertically** — each release is a
fully usable surface, not a half-built layer.

> **History.** v0.1 was a face and person understanding runtime. It produced the
> engine naina still runs on: the C ABI, the backend abstraction, and the
> manifest-driven model loader. v0.2 repurposed that engine for document
> reading, which is what the name always suited — *naina* means eyes. The face
> modules are preserved on the
> [`face-stack`](https://github.com/jvoltci/naina/tree/face-stack) branch. See
> [the design spec](superpowers/specs/2026-07-28-naina-ocr-design.md) for why.

## v0.1 — Engine  *(shipped)*

C ABI, backend abstraction, tensor and arena types, model registry with
sha256-verified downloads, Python and Node bindings, CI matrix.

- [x] `core/include/naina/{naina.h,naina.hpp,backend.hpp,tensor.hpp,model_loader.hpp}`
- [x] `IBackend` / `ISession` for ONNX Runtime and NCNN, runtime probe + fallback
- [x] `model_loader` — manifest parse, HTTP download, sha256 verify, cache
- [x] Python binding (pybind11 + scikit-build-core)
- [x] Node binding (cmake-js + N-API, inference off the event loop)
- [x] CI: Linux gcc + clang, macOS arm64

## v0.2 — Text spotting  *(shipped)*

Detect text, recognise it, return lines with geometry and confidences, from
Python and Node.

- [x] Retier the registry by device (`tiny` / `small` / `medium`) instead of licence
- [x] Registry: PP-OCRv6 det + rec at three tiers, every sha256 verified
- [x] Mirror all weights into naina's own release; upstream URLs kept as provenance
- [x] `image_ops` — detection resize geometry, quad rectification to 48px strips
- [x] `geometry` — convex hull, minimum-area rectangle, convex polygon offset
- [x] `db_postprocess` — binarize, 8-connected blob borders, box scoring, DBNet decode
- [x] `charset` — parse `PostProcess.character_dict`, per-tier class counts
- [x] `ctc_decode` — greedy decode, repeat collapse, blank handling, confidence
- [x] `text_detect` + `text_recognize` module wiring
- [x] `page` — pointer-stable storage, markdown + JSON serialisation
- [x] `naina_read` end to end, verified against real weights
- [x] Python + Node OCR surface
- [x] No OpenCV, no pyclipper, no PaddlePaddle dependency

**Known gaps carried into v0.3:**

- `FindNCNN.cmake` does not locate a Homebrew NCNN install, so only the ONNX
  Runtime backend is exercised in practice.
- Recognition runs one strip per session call. Correct, but batching needs a
  uniform width per batch and would be meaningfully faster on dense pages.

## v0.3 — Layout and markdown  *(mostly shipped)*

Turn a bag of lines into a document.

- [x] `tools/paddle2onnx_layout.py` — exports PP-DocLayout-S/-M, which PaddleOCR
      ships only in Paddle format. Byte-deterministic, and verified per-column
      against the Paddle original (classes exact, scores to 5e-7, box
      coordinates to 3e-4 px). This was the design's biggest open risk: without
      it layout would exist only at the 269 MB tier and the 11 MB tier could not
      describe structure at all.
- [x] Layout at every tier — 11.1 / 54.5 / 268.0 MB totals
- [x] `layout_detect` — region boxes with class labels from each model's own
      23-entry `label_list`, inputs fed BY NAME since the variants disagree on
      signature
- [x] `doc_assemble` — line-to-region assignment, column-aware reading order,
      structured markdown. Pure logic, 16 tests from hand-built inputs.
- [x] MCP server (see v1.0 list)
- [ ] Golden corpus: committed images with expected markdown
- [ ] Recognition batching by padded width
- [ ] Fix `FindNCNN.cmake`
- [ ] Improve small-tier layout recall. Measured on a synthetic report page,
      PP-DocLayout-S at 480x480 found 4 of 7 regions, all scoring barely over
      the 0.5 threshold upstream itself defaults to; section titles and captions
      were missed and fell through to the unstructured tail. The larger tiers
      should be compared before tuning the threshold.

## v0.4 — Browser

- [ ] `bindings/wasm` via Emscripten, code budget < 5 MB compressed
- [ ] PWA at `jvoltci.github.io/naina/` — client-side only, no upload, offline
      after first visit via service worker + Cache API keyed on model sha256
- [ ] mkdocs-material documentation at `jvoltci.github.io/naina/doc/`
- [ ] WebGPU execution provider where available

## v0.5 — Rust

- [ ] `bindings/rust` over the C ABI, published to crates.io
- [ ] Golden corpus passes from Rust

## v1.0 — Guarantees

- [ ] **Cross-binding parity enforced in CI.** Python, Node, Rust and WASM must
      produce byte-identical output for a fixed (backend, device, tier) on the
      golden corpus. Scoped honestly: identical *across bindings*,
      tolerance-bounded *across backends*, because ONNX Runtime and NCNN differ
      in floating-point behaviour.
- [ ] Full benchmark matrix: accuracy and latency per device, harness in-repo,
      reproducible from a clean clone
- [x] MCP server — `mcp/`, two tools (`read_document`,
      `read_document_detailed`), verified end to end over stdio
- [ ] Move the MCP server to spec revision **2026-07-28**, whose stateless
      request/response model suits naina exactly. Blocked on the SDK:
      `@modelcontextprotocol/sdk@1.30.0` tops out at `2025-11-25`, and a client
      asking for 2026-07-28 negotiates down. The server already holds no
      session state, so this should be a dependency bump, not a rewrite.
- [ ] Prebuilt binaries: macOS arm64/x64, Linux x64/arm64, Windows x64, cp39–cp313
- [ ] Vendored ONNX Runtime in published wheels (delocate / auditwheel)

## Non-goals

- **Training or fine-tuning.** naina is inference only.
- **Autoregressive VLM parsing** (PaddleOCR-VL, DeepSeek-OCR). These need a
  tokenizer, KV cache and sampling loop — a different engine, not a module.
  Revisit only as an explicit v2 decision, never as drift.
- **Handwriting.** PP-OCRv6 is weak at it; claiming support would be dishonest.
- **Chart and formula semantics.** Regions get detected and labelled; their
  contents are not interpreted.
- **Face and person understanding.** Parked on `face-stack`, not deleted.
- Vector stores, dashboards, UI frameworks.
- Crime prediction, risk scoring, government-ID matching.
