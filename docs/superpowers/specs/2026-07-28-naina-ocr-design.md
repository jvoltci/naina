# naina — OCR pivot design

**Date:** 2026-07-28
**Status:** approved design, pending implementation plan

## Summary

Repurpose `naina` from a face/person computer-vision runtime into an
**embeddable document-reading runtime**. Keep the engine (C ABI, backend
abstraction, model loader, tensor/arena, Python + Node bindings). Park the
face modules. Add text detection, text recognition, layout analysis, and
markdown assembly.

The name means *eyes* in Hindi. It fits reading better than it fit face
matching, and it drops the biometric-surveillance positioning that would
have made an OCR library hard to adopt.

**One sentence:** naina reads any document into markdown, from one C++ core,
on servers, laptops, phones, Raspberry Pis, and in a browser tab with no
server at all.

## Why this exists

Accuracy in OCR is a solved commodity. PP-OCRv6's weights are Apache-2.0,
so naina runs the same models PaddleOCR runs and gets the same accuracy to
three decimals. Competing on accuracy is unwinnable and pointless.

The gap is **distribution**. Every existing tool is locked into one lane:

| Tool | Lane | Cannot do |
| --- | --- | --- |
| PaddleOCR | Python, server | No Node/Rust/browser/C ABI; training framework first, huge surface |
| RapidOCR | Multi-language as *separate ports* | Behaviour drifts between Python/C++/Java/.NET; no single core |
| oar-ocr | Rust only | No Python/Node bindings, no shipped WASM |
| retto | Rust only, det+rec only | No bindings, no layout |
| client-ocr | Browser only | No server, no native |
| ML Kit (Google Lens) | Mobile only, closed weights | Cannot self-host; 5 scripts only |
| MinerU / marker / docling | Python, GPU-leaning | License traps, heavy installs |

Nobody ships one engine that runs identically everywhere. This is
llama.cpp's playbook applied to OCR: llama.cpp won on portability and zero
dependencies, not on inference math. naina's existing C ABI plus runtime
backend probe is already that architecture.

## The four pillars

1. **One core, every language, identical output.** Python, Node, Rust and
   WASM are thin bindings over one C ABI. CI proves they agree on a fixed
   corpus. No competitor offers this, and copying it means rebuilding their
   whole project.
2. **Runs where nothing else does.** The tiny tier is ~11 MB total. That
   fits a browser tab, a phone, a Pi Zero. A credible OCR library in a
   `<script>` tag with no server does not currently exist.
3. **Honest, reproducible benchmarks.** Vendors self-report 96.33% on
   OmniDocBench v1.6 while independent evaluation of the same benchmark tops
   out at 90.1%. naina publishes per-device latency and accuracy with the
   harness in-repo and the command to reproduce it.
4. **Agent-native.** `naina.read(path) -> markdown` is the primary API, plus
   an MCP server. Not a 40-function structure-analysis surface.

   Target **MCP spec 2026-07-28**, released the day this design was written.
   It replaces the bidirectional stateful protocol with a **stateless
   request/response model**, which suits naina exactly — reading a document
   carries no session state, so the server deploys to serverless or edge with
   no session management. Two consequences for the v1.0 MCP plan:

   - Build against the stateless core. Do not carry session state between
     calls; each `read` is self-contained.
   - **MCP Apps** (interactive UI rendered in the conversation) is the right
     surface for showing detected regions and letting a user correct reading
     order, rather than dumping coordinates as text. Treat this as optional
     polish, not v1.0 scope.

   Authorization in this spec version aligns with production OAuth 2.0 / OIDC.
   naina's MCP server reads local files and needs no auth, so this is not
   relevant unless a hosted variant is ever offered.

### Scoping pillar 1 honestly

"Byte-identical" is only truthful when scoped. Floating-point operations
differ between ONNX Runtime and NCNN, and between CPU and GPU execution
providers. The guarantee is therefore:

- **Byte-identical** across *bindings* for a fixed (backend, device, tier,
  model version). Python, Node, Rust and WASM must produce the same bytes.
- **Tolerance-bounded** across *backends*: character error rate within a
  documented epsilon, asserted in CI.

Claiming more than this in the README would be dishonest and would not
survive a bug report.

## Models

All Apache-2.0. Three device tiers, verified sizes:

| Tier | det | rec | layout | Total | Target |
| --- | --- | --- | --- | --- | --- |
| `tiny` | 1.78 MB | 4.46 MB | 4.80 MB* | **≈ 11 MB** | Browser, phone, Pi Zero |
| `small` | 9.88 MB | 21.16 MB | 23.37 MB* | **≈ 54 MB** | Laptop, Pi 5, mobile app |
| `medium` | 62.03 MB | 76.55 MB | 130.50 MB | **≈ 269 MB** | Server, desktop |

Sources: `PaddlePaddle/PP-OCRv6_{tiny,small,medium}_{det,rec}_onnx`,
`PaddlePaddle/PP-DocLayoutV3_onnx`, `PaddlePaddle/PP-DocLayout-{S,M}`.

`*` PP-DocLayout-S and -M ship only in Paddle format and need a
`paddle2onnx` export step. **This is a risk** — the export must be verified
to produce numerically equivalent output before the tiny and small tiers can
claim layout support. If export fails, those tiers ship text-spotting only
and layout requires `medium`.

Language coverage: 50 languages for the small and medium recognition models
(Simplified Chinese, Traditional Chinese, English, Japanese, and 46
Latin-script languages).

### Registry

Reuse the existing `models/registry.yaml` manifest pattern unchanged: URL,
sha256, byte count, preprocessing, postprocessing, license, benchmarks.
Replace the face entries with nine OCR entries (3 tiers × 3 models).

The existing `tier: default | research` axis was a *licensing* distinction.
OCR has no licensing split — everything is Apache-2.0 — so the axis becomes
a *device* distinction: `tiny | small | medium`. This is a schema change to
`models/manifest.schema.json`, not just new data.

## Architecture

Five modules. Only three touch a model.

```
image ──▶ text_detect ──▶ text_rectify ──▶ text_recognize ──┐
  │        (model)         (pure CV)          (model)        │
  │                                                          ▼
  └────▶ layout_detect ─────────────────────▶ doc_assemble ──▶ markdown
           (model)                             (pure logic)
```

| Module | Model | Responsibility |
| --- | --- | --- |
| `text_detect` | PP-OCRv6 det | DBNet-style segmentation map → quad polygons |
| `text_rectify` | none | Crop each quad, perspective-warp to fixed-height strip |
| `text_recognize` | PP-OCRv6 rec | Batched CTC decode → UTF-8 string + confidence |
| `layout_detect` | PP-DocLayout | Region boxes with class labels |
| `doc_assemble` | none | Assign lines to regions, order regions, emit markdown |

`text_rectify` and `doc_assemble` are deterministic pure functions with no
model and no randomness. That is deliberate: it makes the cross-binding
identity guarantee provable with plain equality assertions rather than
float-tolerance comparisons.

### Why this maps onto the existing engine

The face pipeline was `detect → align → embed`: find regions, warp each one,
run a second model per crop. The OCR pipeline is `detect → rectify →
recognize` — the same shape. Batching, arena allocation, and crop-and-rerun
control flow transfer directly. `layout_detect` is a second independent
detector. `doc_assemble` is pure logic on top.

This is not a new engine. It is two detectors, one recognizer, and a
serializer added to an engine already measured at 3.76 ms/detect on an M3 Pro.

## C ABI additions

Matches the existing conventions in `core/include/naina/naina.h`: status
codes, opaque handles, out-params, lib-allocated outputs with explicit free
functions, versioned config.

```c
/* ─── OCR enums ───────────────────────────────────────────────────── */

typedef enum {
    NAINA_TIER_AUTO = 0,   /* pick by available memory + device */
    NAINA_TIER_TINY,
    NAINA_TIER_SMALL,
    NAINA_TIER_MEDIUM,
} naina_tier;

typedef enum {
    NAINA_REGION_UNKNOWN = 0,
    NAINA_REGION_TITLE,
    NAINA_REGION_TEXT,
    NAINA_REGION_LIST,
    NAINA_REGION_TABLE,
    NAINA_REGION_FIGURE,
    NAINA_REGION_CAPTION,
    NAINA_REGION_FORMULA,
    NAINA_REGION_HEADER,
    NAINA_REGION_FOOTER,
    NAINA_REGION_PAGENUM,
} naina_region_kind;

/* ─── OCR POD types ───────────────────────────────────────────────── */

typedef struct {
    naina_point corners[4];   /* clockwise from top-left */
    float score;
} naina_textbox;

typedef struct {
    naina_textbox box;
    const char* text;         /* UTF-8, NUL-terminated, owned by page */
    float confidence;
    int32_t region_id;        /* index into regions, -1 if unassigned */
} naina_textline;

typedef struct {
    naina_bbox bbox;
    naina_region_kind kind;
    int32_t order;            /* reading-order index */
} naina_region;

typedef struct naina_page naina_page_t;   /* opaque; owns lines, regions, strings */

/* ─── One-shot read (the primary API) ─────────────────────────────── */

NAINA_API naina_status naina_read(naina_ctx_t* ctx,
                                  const naina_image_t* image,
                                  naina_page_t** out_page);
NAINA_API void naina_page_release(naina_page_t* page);

NAINA_API naina_status naina_page_lines(const naina_page_t* page,
                                        const naina_textline** out_lines,
                                        int32_t* out_count);
NAINA_API naina_status naina_page_regions(const naina_page_t* page,
                                          const naina_region** out_regions,
                                          int32_t* out_count);
NAINA_API const char* naina_page_markdown(const naina_page_t* page);
NAINA_API const char* naina_page_json(const naina_page_t* page);

/* ─── Stage-level access (for users who want the pieces) ──────────── */

NAINA_API naina_status naina_text_detect(naina_ctx_t* ctx,
                                          const naina_image_t* image,
                                          naina_textbox** out_boxes,
                                          int32_t* out_count);
NAINA_API void naina_free_textboxes(naina_textbox* boxes, int32_t count);

NAINA_API naina_status naina_layout_detect(naina_ctx_t* ctx,
                                            const naina_image_t* image,
                                            naina_region** out_regions,
                                            int32_t* out_count);
NAINA_API void naina_free_regions(naina_region* regions, int32_t count);
```

`naina_config` gains one field: `naina_tier tier;`. Per the header's own ABI
rules, adding a field requires bumping the config `version` to 2 and keeping
version 1 accepted.

`naina_page_t` owning all strings in one arena avoids per-string malloc
churn across the boundary, and gives bindings a single release call.

## Repository transformation

```
KEEP UNCHANGED  (the reason we are reusing naina)
  core/include/naina/{backend,tensor,model_loader}.hpp
  core/src/backend_registry.cc  tensor.cc  sha256.cc  version.cc
  core/src/backends/onnxruntime_backend.cc
  core/src/backends/ncnn_backend.cc
  core/src/model_loader.cc

PARK on branch `face-stack`, then remove from master
  core/src/modules/face_detect.{cc,hpp}
  core/src/modules/face_embed.{cc,hpp}
  core/src/modules/face_liveness.{cc,hpp}
  examples/{python,node}/face_verify.*

EXTEND
  core/src/image_ops.cc          + perspective warp, binarize, contour trace
  core/include/naina/naina.h     swap face ABI for OCR ABI (config version 2)
  core/src/api.cc  api_cpp.cc    wire the new entry points
  models/registry.yaml           9 OCR entries
  models/manifest.schema.json    tier axis: device, not licence

ADD
  core/src/modules/text_detect.{cc,hpp}
  core/src/modules/text_rectify.{cc,hpp}
  core/src/modules/text_recognize.{cc,hpp}
  core/src/modules/layout_detect.{cc,hpp}
  core/src/modules/doc_assemble.{cc,hpp}
  tools/paddle2onnx.py
```

### Monorepo layout

One repo, every platform.

```
naina/
  core/                 C++20 engine + C ABI
  bindings/
    python/             exists — pybind11 + scikit-build-core → PyPI `naina`
    node/               exists — cmake-js + N-API      → npm `naina` (see risk 4)
    rust/               NEW    — bindgen over C ABI     → crates.io `naina`
    wasm/               NEW    — Emscripten             → npm `naina-wasm`
  app/                  NEW    — the PWA (Vite + TS), migrated from demo/web
  mcp/                  NEW    — MCP server (Node, thin over the node binding)
  docs/                 mkdocs-material site
  models/               registry.yaml + schema
  benchmarks/           harness + committed results per device
  tools/                paddle2onnx.py, onnx2ncnn.py
  examples/             python/ node/ rust/ browser/ cli/
  tests/golden/         the cross-binding parity corpus
```

CLI ships as a console entry point on the Python and Node packages rather
than a separate `cli/` tree — less surface, same result.

## GitHub Pages: app at the root, docs beneath

Both are wanted; one Pages site per repo. **The app is the front door** — a
visitor should be able to read a document before reading a word of prose.

```
jvoltci.github.io/naina/         → the in-browser OCR PWA
jvoltci.github.io/naina/doc/     → mkdocs-material documentation
```

Build order in the Pages workflow, into a single artifact:

1. `mkdocs build --site-dir _site/doc` — with `site_url` set to
   `https://jvoltci.github.io/naina/doc/` so internal links and the search
   index resolve correctly.
2. Vite build of `app/` into `_site/` with `base: '/naina/'`.
3. One `actions/upload-pages-artifact` on `_site`.

Two details this ordering forces:

- The service worker is scoped to `/naina/` and must **not** intercept
  `/naina/doc/*`, or offline caching will swallow the docs. Exclude the
  `doc/` prefix explicitly in the SW route config.
- Vite must not clean `_site/`, since mkdocs wrote `doc/` there first. Set
  `emptyOutDir: false`.

### The app

- Runs entirely client-side on `bindings/wasm`. No upload, no server, no
  telemetry — which is a real privacy story worth stating plainly.
- **Offline after first visit.** Service worker precaches the app shell and
  the WASM binary. Model weights (~11 MB tiny tier) are fetched once and
  stored in the **Cache API**, keyed by the registry sha256 so a model
  version bump invalidates cleanly. Second visit works with the network off.
- Drag-drop or paste an image or PDF, get markdown, copy or download.
- Tier selector, with an honest note that tiny trades accuracy for size.

**Note on "commercial":** GitHub Pages' terms discourage using it as free
hosting for a business. A free tool published alongside an open-source
project is fine. If this is ever monetised, plan to move the app to
Cloudflare Pages or Netlify — the build is a static bundle, so the move is
cheap. Flagging this now so it is not a surprise later.

## README standard

Match the `breccia` pattern exactly, since that is the established bar:

1. Animated hero SVG (`docs/assets/hero.svg`, `viewBox="0 0 850 380"`,
   Outfit + JetBrains Mono via `@import`, CSS keyframe animation), centered,
   `width="100%"`
2. Badge row: PyPI version, npm version, crates.io version, license, CI,
   docs, stars
3. One-line `<h3 align="center">` tagline
4. Link row: Docs · Try in browser · PyPI · npm · crates.io · Discussions
5. A code block immediately, no preamble
6. `## Why` — the lane-lock table above, competitors named
7. `## What you get` — tier table, language table, binding table
8. `## Benchmarks` — real numbers, each with `Reproduce: <command>`
9. `## Status` — component table with ✅
10. `## Install` — per ecosystem
11. `## Examples`, `## Documentation` — links
12. `## The name` — naina means eyes in Hindi
13. `## Contributing`, `## License`

Also needed: `docs/assets/logo.svg`, `docs/assets/favicon.svg`,
`docs/assets/extra.css`, `CHANGELOG.md`, `CONTRIBUTING.md`, `RELEASE.md`,
`.github/ISSUE_TEMPLATE/bug.yml`.

## Documentation site

mkdocs-material, same theme configuration as breccia (custom palette, Inter
+ JetBrains Mono, instant navigation, mermaid via superfences).

```
Home                     index.md
Get started              install.md  quickstart.md  tiers.md
Guides                   markdown-output.md  languages.md  browser.md
                         mobile-edge.md  agents-mcp.md
Reference                api-c.md  api-python.md  api-node.md  api-rust.md
                         api-wasm.md  architecture.md  registry.md
Benchmarks               benchmarks.md
Compare                  vs-paddleocr.md  vs-tesseract.md  vs-cloud.md
FAQ                      faq.md
```

`api-python.md` uses mkdocstrings. The other bindings get hand-written
reference pages generated from the C ABI header.

## Testing strategy

| Layer | What it proves |
| --- | --- |
| Unit (C++, existing gtest setup) | Each module in isolation; `text_rectify` and `doc_assemble` exactly, being pure |
| Golden corpus | A committed set of images with expected markdown. Small, hand-verified, in-repo |
| Cross-binding parity | Python/Node/Rust/WASM run the golden corpus; outputs must be byte-identical for a fixed backend+tier. **This is pillar 1, enforced in CI** |
| Cross-backend tolerance | ONNX Runtime vs NCNN on the golden corpus; CER within documented epsilon |
| Accuracy eval | OmniDocBench + olmOCR-Bench subsets, reproducible, published |
| Latency | Per device, committed as JSON like the existing `benchmarks/results/m3-pro-default.json` |

## Distribution

The unglamorous 80% of the work.

| Artifact | Targets |
| --- | --- |
| Python wheels | macOS arm64/x64, Linux x64/arm64 (manylinux), Windows x64; cp39–cp313 |
| Node prebuilds | Same matrix, per Node ABI |
| Rust crate | Source + `build.rs`; vendored or system libnaina |
| WASM | Single `.wasm` + JS glue, code budget < 5 MB compressed |

CI matrix already exists for build+smoke; it needs extending to produce and
publish these artifacts.

## Release sequencing

Each release is usable, not a half-built layer.

| Version | Contents | Done when |
| --- | --- | --- |
| **v0.2** | `text_detect` + `text_rectify` + `text_recognize`; 3 tiers in registry; Python + Node | Text spotting works end-to-end on all three tiers |
| **v0.3** | `layout_detect` + `doc_assemble`; markdown + JSON output; golden corpus | `naina.read(f)` returns correct markdown for the corpus |
| **v0.4** | WASM binding; PWA app; docs site; both on GitHub Pages | App reads a document offline on second visit |
| **v0.5** | Rust binding → crates.io | Golden corpus passes from Rust |
| **v1.0** | Cross-binding parity enforced in CI; full benchmark matrix published; MCP server; prebuilt binaries everywhere | All four bindings byte-identical in CI; benchmarks reproducible from a clean clone |

### Scope of the first implementation plan

This spec spans five releases. The implementation plan that follows covers
**v0.2 and v0.3 only** — the OCR core through markdown output on Python and
Node. WASM, the app, the docs site, the Rust binding and MCP each get their
own plan once the core is proven. Trying to plan all of v1.0 at once would
produce a plan too coarse to execute against.

## Non-goals

- Training or fine-tuning. naina is inference only.
- Autoregressive VLM parsing (PaddleOCR-VL, DeepSeek-OCR). This needs
  tokenizer, KV cache and a sampling loop — a different engine, not a
  module. Revisit for v2 as an explicit decision, not a drift.
- Handwriting. PP-OCRv6 is weak at it; claiming it would be dishonest.
- Chart and formula *semantic* extraction. Regions get detected and labelled;
  contents are not interpreted.
- Face and person understanding. Parked on a branch, not deleted.
- Vector stores, dashboards, UI frameworks.

## Open risks

1. **`paddle2onnx` export of PP-DocLayout-S/-M.** Load-bearing for layout on
   the tiny and small tiers. Verify numerical equivalence early. Fallback:
   layout requires `medium`.
2. **WASM bundle budget.** ONNX Runtime's WASM build plus glue must stay
   under 5 MB compressed. Unverified. Fallback: ship a reduced operator set.
3. **Cross-binding byte-identity.** Achievable in principle for a fixed
   backend, but threading and SIMD paths can perturb results. May require
   pinning thread counts in the parity test.
4. **npm name.** `naina` is held by an abandoned package (last publish May
   2022, 6 downloads/week). Dispute filed; `@jvoltci/naina` is the fallback
   and blocks nothing.
5. **Maintenance load.** A multi-platform C++ library with four bindings is a
   large standing CI and release commitment. This is the actual cost of the
   "leads the field" goal, and it is ongoing rather than one-time.
