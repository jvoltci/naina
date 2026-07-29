# Roadmap

Versioned, contributor-facing. naina ships **vertically** — each release is a
fully usable surface, not a half-built layer.

> **History.** v0.1 was a face and person understanding runtime. It produced the
> engine naina still runs on: the C ABI, the backend abstraction, and the
> manifest-driven model loader. v0.2 repurposed that engine for document
> reading, which is what the name always suited — *naina* means eyes. The face
> modules are preserved on the
> [`face-stack`](https://github.com/jvoltci/naina/tree/face-stack) branch. See
> [the design spec](https://github.com/jvoltci/naina/blob/master/docs/superpowers/specs/2026-07-28-naina-ocr-design.md) for why.

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
- [x] Medium tier verified end to end on an A4 academic page: 14 of 14 regions
      detected **and correctly labelled**, reading order correct, 33 of 33 lines
      recognised at 0.99–1.00, markdown with the right heading hierarchy and the
      running head omitted as furniture.
- [ ] Golden corpus: committed images with expected markdown
- [ ] Recognition batching by padded width
- [ ] Fix `FindNCNN.cmake`
- [ ] **Cross-class NMS.** PaddleDetection runs NMS per class, so one box comes
      back under several labels and naina keeps every row over threshold
      independently. Measured: a running head returned `text` at 0.677 *and*
      `header` at 0.481 for the identical box. Whenever two labels for one box
      both clear the threshold, that box yields two regions — and the
      higher-scoring label is not always the right one. Needs a dedup pass
      keeping the best-scoring class per box group.
- [ ] Improve layout recall on out-of-distribution pages. PP-DocLayout is
      trained on papers and reports and is excellent there (see above), but
      degrades sharply outside that shape. On a synthetic wide-spaced report page
      it labelled a body paragraph `doc_title` and both section headings `text`,
      producing structurally wrong markdown; small tier at 480x480 found only 4
      of 7 regions on the same page. The correct label was usually present in the
      raw rows at a lower score, which suggests cross-class NMS above may recover
      some of this on its own — do that first, then reassess the threshold.
- [x] **Devanagari support.** Hindi, Marathi, Nepali and Sanskrit, via a
      `language` axis orthogonal to tier. Verified end to end: the page that
      previously returned `3rarearanlus Tarafaaa: f:` at 0.758 confidence now
      returns `अयोध्याकाण्डे नवनवतितम: सग्गः`, 129 lines at 0.90–0.99 native and
      2731 Devanagari characters in a real browser.

      Detection and layout are shared, not duplicated — they are script-agnostic,
      which is why this was a registry change rather than engine work.
      `config.version` 3 appends a `language` string; existing offsets are
      unchanged (40 → 48 bytes) so v1 and v2 configs still work, asserted by test.
      An unknown language returns `NAINA_E_UNSUPPORTED` rather than falling back
      to Latin.

- [x] **Ten alphabets.** Default (Latin, Chinese, Japanese) plus arabic,
      cyrillic, devanagari, el, eslav, korean, ta, te, th — every script upstream
      ships as ONNX. Each was verified against naina's recognition path before
      being added (`[N,3,48,W]`, `CTCLabelDecode`, `num_classes == dict + 2`);
      all nine matched, none needed a code change. Mirrored into naina's release
      and sha256-verified by re-download. Registry entries are generated from the
      files rather than hand-written, so a hash cannot be mistyped.
      Verified in a real browser: Greek returned `Ελληνικά κείμενο 2026` and
      Cyrillic `Русский текст 2026`, both exact.

- [x] **Script auto-detection in the web app.** Reads with the default, and if
      mean confidence is under 0.95 re-reads a 900px copy with each other
      alphabet, keeping the best if it clears the default by 0.03. Verified on
      four scripts: Latin correctly not switched (and free — the gate never
      trips, 3.6s), Hindi → devanagari, Greek → el, Cyrillic → cyrillic.

      The thresholds come from measurement, not guesswork. Best-alphabet margins
      over the default: Hindi +0.426, Cyrillic +0.104, Greek +0.066, Latin +0.006
      (where `arabic` scored highest, since every alphabet contains Latin — which
      is why a plain argmax is wrong and a margin is required). 0.03 has 2x
      headroom under the tightest true positive and 5x over the Latin tie. An
      early exit at +0.25 cut the Hindi case from 130s to 28s.

      Found and fixed a real WASM binding bug in the process: the JS bridge was
      installed on `globalThis`, so a second Reader overwrote the first one's and
      an older Reader read tensor descriptors from the wrong WASM heap. It is now
      per-module.

- [ ] **Auto-detection in the libraries.** Needs every alphabet's weights present
      (~70 MB), which defeats an 11 MB tier, and needs the context's session and
      charset caches re-keyed by language. Probably belongs as an opt-in taking an
      explicit candidate list rather than as a default.

## v0.4 — Browser  *(binding shipped)*

- [x] `bindings/wasm` via Emscripten. **143 KB brotli** for `naina.wasm` plus
      24 KB for the JS glue, against a 5 MB budget — the no-OpenCV rule is what
      makes that possible, since OpenCV alone would have exceeded it.
- [x] `wasmjs_backend` — the only thing crossing into JS is
      `ISession::run`, i.e. "execute this graph on these tensors". All of
      naina's own arithmetic (preprocessing, DB decode, rectification, CTC,
      layout, doc_assemble) runs the same C++ compiled to WASM. Uses ASYNCIFY
      because ort-web's `run()` is a Promise while `ISession::run` is
      synchronous; the alternative (SharedArrayBuffer + `Atomics.wait`) needs
      COOP/COEP headers that GitHub Pages cannot set.
- [x] Model staging: JS fetches through the Cache API, writes into Emscripten's
      virtual FS, and the shared C++ core sha256-verifies every file — so the
      browser gets the same integrity check as every other platform, not a
      weaker one. The staging plan (URLs *and* cache paths) is computed by C++
      via `stagingPlan()`, so the cache layout has one definition.
- [x] Verified reading a real A4 page: 33 lines, mean confidence 0.99,
      correct `#`/`##` structure, deterministic across runs.
- [x] Production web app at `jvoltci.github.io/naina/` — client-side only, no
      upload, no account, no page limit. PDFs via pdf.js, multi-page batches, OCR
      in a Web Worker so the tab never freezes, offline after first visit.
      Verified end to end in real Chrome via Playwright (`app/test/e2e.mjs`),
      which is the only test that can reach OffscreenCanvas, createImageBitmap,
      module workers and ASYNCIFY-in-a-worker.
- [x] mkdocs-material documentation at `jvoltci.github.io/naina/doc/`
- [x] **Weights are served same-origin, not from the GitHub release.** Release
      assets 302 to release-assets.githubusercontent.com and neither hop sends
      `Access-Control-Allow-Origin`, so a browser cannot fetch them at all —
      measured against the live release. Every other binding is unaffected. The
      deploy stages tiny + small (65.9 MB) into the Pages artifact, reading the
      file list from the core via `stagingPlan()` so it cannot drift. sha256
      verification still happens in the C++ core.
- [ ] WebGPU execution provider. **Currently off by default because it silently
      breaks layout, which is worse than crashing.** Chrome 141 on an M3
      initialises ORT's JSEP provider and then fails a MatMul kernel; ORT recovers
      node by node, so text still returned 33 lines at 0.99 confidence and looked
      correct, while layout regions went from 9 to 0 and the markdown lost all
      structure. Needs real numbers before it becomes a default.
- [ ] Medium tier in the browser. `ppdoclayout_l.onnx` is 129 MB and GitHub Pages
      caps a single file at 100 MB, so the app offers tiny and small only.

## v0.5 — Rust  *(built)*

- [x] `bindings/rust` over the C ABI. Safe wrapper with the unsafe surface
      confined to `src/ffi.rs` and the marshalling in `read_rgb`. 13 tests pass
      against a real `libnaina`, including reading a page end to end and
      asserting `naina_config` is 48 bytes with 8-byte alignment — matching the C
      compiler, because a mismatch there would write `tier` into the wrong slot
      and silently load a different model.
- [x] `Reader` is `Send` but **not** `Sync`: the native context is not internally
      synchronised and concurrent reads would race on its session cache.
- [x] Vendors the C++ core for a self-contained crate (`vendor.sh`), but
      deliberately **not** ONNX Runtime — ~17 MB per platform with its own floors,
      and four bundled copies would inherit all of that silently.
- [ ] Published to crates.io. Needs `cargo publish` with a token.
- [ ] Golden corpus passes from Rust

## v1.0 — Guarantees

- [ ] **Cross-binding parity enforced in CI.** Python, Node and Rust must produce
      byte-identical output for a fixed (backend, device, tier) on the golden
      corpus — they run the same compiled core against the same kernels, so
      anything less is a bug.

      **WASM is explicitly outside that set, and this is measured, not assumed.**
      onnxruntime-web is a different *build* of ONNX Runtime — WASM SIMD kernels
      rather than native NEON/AVX — so its probability maps differ in the last
      few float bits. On the A4 fixture at tiny tier that moved one marginal blob
      across DBNet's 0.3 binarize threshold: native macOS arm64 produced 35 text
      lines, WASM 33, with 33 character-identical. Because a split fragment takes
      its own reading-order slot, word order can shift with it too. An earlier
      draft of this file listed WASM in the byte-identical group; that was wrong.
      What WASM does guarantee is determinism within itself (same input, same
      output — asserted in `bindings/wasm/test/read.test.mjs`) and the same
      *algorithms*, since it runs the same C++.
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
