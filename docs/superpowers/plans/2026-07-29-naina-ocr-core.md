# naina OCR Core — v0.2 Text Spotting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn naina's existing C++ inference engine into a text spotter — detect text quads in an image, recognise the characters in each, and return them through the C ABI to the existing Python and Node bindings.

**Scope boundary:** This plan is **v0.2 only**. Layout analysis, reading order and markdown assembly (`layout_detect`, `doc_assemble`, `naina_page_markdown`) are **v0.3** and get their own plan once text spotting is proven end-to-end. The v0.2 ABI still declares the v0.3 entry points — they were stubbed in Task 4 and stay `NAINA_E_UNSUPPORTED` until that plan runs. Task 16 verifies the layout model export early, because it is the design's biggest open risk and a bad answer changes the v0.3 plan.

**Architecture:** Three new internal modules on the unchanged backend/session/registry layer, plus two pure support units. `text_detect` (DBNet probability map → quads) sits on `geometry` and `db_postprocess`; `text_rectify` perspective-warps each quad to a 48px strip; `text_recognize` batches those strips through a CTC head and greedy-decodes. Three device tiers replace the old licence-based tier axis. An arena-backed `naina_page_t` owns all output strings so bindings need one release call.

**Tech Stack:** C++20, ONNX Runtime / NCNN via the existing `IBackend`/`ISession` abstraction, yaml-cpp for the registry, libcurl for weight downloads (already implemented), pybind11 + scikit-build-core for Python, cmake-js + N-API for Node. Tests are plain executables using the repo's `EXPECT` macro — **there is no gtest in this project.**

**Branch:** `ocr-pivot` (already created, spec already committed as `6de7e7b`).

---

## Read this before writing any code

This project compiles with **`-Werror`** and a strict warning set, verified
from the live build command:

```
-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
-Wnon-virtual-dtor -Wold-style-cast -Wnull-dereference -Wdouble-promotion
-Wformat=2 -Werror
```

Every one of these is an error, not a warning. Consequences for the code in
this plan:

- **`-Wsign-conversion`** — mixing `size_t` with `int32_t` in index arithmetic
  fails. Cast *every* operand, not just the first:
  `bm.px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]`.
  A partial cast like `static_cast<size_t>(y) * w + x` still errors.
- **`-Wconversion`** — no implicit narrowing. `int32_t x = some_float;` fails;
  use `static_cast<int32_t>(std::lround(f))`.
- **`-Wdouble-promotion`** — a bare literal like `0.5` is a `double` and
  promotes. Write `0.5F`. `std::sqrt`/`std::fabs`/`std::hypot` on `float`
  arguments are fine; passing a `double` literal alongside a `float` is not.
- **`-Wold-style-cast`** — no `(int)x`. Always `static_cast`.
- **`-Wshadow`** — a local may not shadow a parameter or outer local. Watch
  loop variables named `x`/`y` inside functions that already take `x`/`y`.

The code blocks in the tasks below were written with these rules in mind, but
**verify by building, not by reading.** If a build fails on a conversion
warning, fix the cast — do not add `-Wno-*` and do not change the project's
warning flags.

---

## Verified reference data

Everything below was probed from the real artifacts on 2026-07-29. Do not
re-derive it; do not substitute guesses.

### PP-OCRv6 detection model

```
opset 14, ir_version 10
INPUT   name "x"             FLOAT  [N, 3, H, W]     all of N/H/W dynamic
OUTPUT  name "fetch_name_0"  FLOAT  [N, 1, H, W]     single-channel probability map
242 nodes; head ends in 2x ConvTranspose (DBNet upsampling)
TensorRT shape hints: min 1x3x32x32, opt 1x3x736x736, max 1x3x4000x4000
```

Preprocess (from `inference.yml`): decode **BGR**, normalise `order: hwc`
with `scale = 1/255`, `mean = [0.485, 0.456, 0.406]`,
`std = [0.229, 0.224, 0.225]`, then to CHW. Resize is `DetResizeForTest`
with default params, i.e. **longest side clamped to 960, both dims rounded
to a multiple of 32**.

Postprocess (`DBPostProcess`): `thresh = 0.2` (binarisation),
`box_thresh = 0.4` (box score filter), `unclip_ratio = 1.4`,
`max_candidates = 3000`.

### PP-OCRv6 recognition model

```
opset 11, ir_version 6
INPUT   name "x"             FLOAT  [N, 3, 48, W]    height FIXED at 48, W dynamic
OUTPUT  name "fetch_name_0"  FLOAT  [N, T, 6906]     T ≈ W/8
219 nodes
```

`6906 = 1 CTC blank + 6905 characters`. Postprocess is `CTCLabelDecode`;
PaddleOCR places blank at **index 0**, so charset entry `i` maps to class
`i + 1`. The charset is the `PostProcess.character_dict` list in the rec
model's `inference.yml` (6905 single-character entries).

### Model registry facts

All Apache-2.0. sha256 values verified by downloading and hashing.

| Tier | Kind | File | bytes | sha256 |
| --- | --- | --- | --- | --- |
| tiny | det | `PP-OCRv6_tiny_det_onnx/inference.onnx` | 1780590 | `193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8` |
| tiny | rec | `PP-OCRv6_tiny_rec_onnx/inference.onnx` | 4462639 | `9ef676d6ed3c88256a2d92c640c44f25b0c40947e111b14b8be8f594091563e6` |
| small | det | `PP-OCRv6_small_det_onnx/inference.onnx` | 9880512 | `d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e` |
| small | rec | `PP-OCRv6_small_rec_onnx/inference.onnx` | 21159378 | `5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634` |
| medium | det | `PP-OCRv6_medium_det_onnx/inference.onnx` | 62032837 | `eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1` |
| medium | rec | `PP-OCRv6_medium_rec_onnx/inference.onnx` | 76554979 | `9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba` |
| medium | layout | `PP-DocLayoutV3_onnx/inference.onnx` | 130502049 | `45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba` |

URL template: `https://huggingface.co/PaddlePaddle/<repo>/resolve/main/inference.onnx`

`PP-DocLayout-S` (4.8 MB) and `-M` (23.4 MB) ship **only** in Paddle format
(`inference.json` graph + `inference.pdiparams` weights) and require a
`paddle2onnx` export — see Task 16. Until that lands, the `tiny` and `small`
tiers have no layout model and `naina_read` falls back to text-only output
for them.

---

## File structure

| Path | Responsibility |
| --- | --- |
| `core/src/image_ops.{hpp,cc}` | **Modify.** Add `resize_det_bgr_planar_f32`, `warp_quad_bgr_planar_f32` |
| `core/src/modules/db_postprocess.{hpp,cc}` | **Create.** Pure DBNet decode: threshold → contours → quads. No model, no session |
| `core/src/modules/text_detect.{hpp,cc}` | **Create.** Session wiring for the det model |
| `core/src/modules/text_rectify.{hpp,cc}` | **Create.** Quad → 48px strip batch. Pure |
| `core/src/modules/ctc_decode.{hpp,cc}` | **Create.** Pure CTC greedy decode + charset type |
| `core/src/modules/text_recognize.{hpp,cc}` | **Create.** Batched rec session wiring |
| `core/src/modules/layout_detect.{hpp,cc}` | **Create.** Layout model wiring + NMS |
| `core/src/modules/doc_assemble.{hpp,cc}` | **Create.** Lines + regions → markdown. Pure |
| `core/src/page.{hpp,cc}` | **Create.** Arena-backed `naina_page` storage |
| `core/include/naina/naina.h` | **Modify.** OCR ABI, config v2, remove face decls |
| `core/include/naina/model_loader.hpp` | **Modify.** `Tier` becomes `{Tiny,Small,Medium}` |
| `core/src/model_loader.cc` | **Modify.** Parse the new tier names |
| `core/src/api.cc` | **Modify.** Wire OCR entry points, drop face ones |
| `core/src/api_cpp.cc` | **Modify.** C++ wrapper for the new surface |
| `models/registry.yaml` | **Modify.** Replace face entries with 7 OCR entries |
| `models/manifest.schema.json` | **Modify.** Tier enum is device-based |
| `core/tests/test_db_postprocess.cc` | **Create.** |
| `core/tests/test_image_ops_warp.cc` | **Create.** |
| `core/tests/test_ctc_decode.cc` | **Create.** |
| `core/tests/test_doc_assemble.cc` | **Create.** |
| `core/tests/test_page.cc` | **Create.** |
| `core/tests/test_ocr_e2e.cc` | **Create.** End-to-end, skips if weights absent |
| `tools/paddle2onnx_layout.py` | **Create.** Export + verify PP-DocLayout-S/M |

Ordering principle: every pure algorithm (`db_postprocess`, `ctc_decode`,
`doc_assemble`, warp) is built and tested **before** anything that needs a
model file on disk. That keeps the first two thirds of this plan runnable
with no network and no weights.

---

## Task 1: Park the face stack

**Files:**
- Create: branch `face-stack`
- Delete: `core/src/modules/face_detect.{cc,hpp}`, `face_embed.{cc,hpp}`, `face_liveness.{cc,hpp}`
- Delete: `examples/python/face_verify.py`, `examples/node/face_verify.mjs`
- Modify: `core/CMakeLists.txt:16-28`

- [ ] **Step 1: Preserve the face work on its own branch**

```bash
cd /Users/shivya/Documents/code/naina
git branch face-stack master
git branch --list
```

Expected: `face-stack`, `master`, and `* ocr-pivot` all listed. The face code
now lives permanently on `face-stack`; deleting it from `ocr-pivot` loses
nothing.

- [ ] **Step 2: Remove the face modules and examples**

```bash
git rm -q core/src/modules/face_detect.cc core/src/modules/face_detect.hpp \
           core/src/modules/face_embed.cc core/src/modules/face_embed.hpp \
           core/src/modules/face_liveness.cc core/src/modules/face_liveness.hpp \
           examples/python/face_verify.py examples/node/face_verify.mjs
```

- [ ] **Step 3: Drop them from the build**

In `core/CMakeLists.txt`, the `NAINA_SOURCES` list currently ends with three
face lines. Delete exactly these three lines:

```cmake
    src/modules/face_detect.cc
    src/modules/face_embed.cc
    src/modules/face_liveness.cc
```

- [ ] **Step 4: Verify the tree still configures**

```bash
cmake --preset macos-arm64 2>&1 | tail -5
```

Expected: configuration succeeds. The build will **not** yet compile, because
`api.cc` still `#include`s the deleted headers — that is fixed in Task 4. Do
not try to build here.

- [ ] **Step 5: Commit**

```bash
git add -A core/CMakeLists.txt
git commit -m "Park face stack on branch face-stack

Face detection, embedding and liveness move to the face-stack branch.
naina is becoming a document reader; the biometric positioning would
have made an OCR library hard to adopt. Nothing is lost — face-stack
holds the full history."
```

---

## Task 2: Retier the model registry

The existing `Tier` enum encodes a **licence** distinction (`Default` =
permissive, `Research` = non-commercial). Every OCR model is Apache-2.0, so
that axis is meaningless here. It becomes a **device** distinction.

**Files:**
- Modify: `core/include/naina/model_loader.hpp:23`
- Modify: `core/src/model_loader.cc` (tier parsing)
- Test: `core/tests/test_model_loader.cc`

- [ ] **Step 1: Write the failing test**

Append to `core/tests/test_model_loader.cc`, immediately before its `main`:

```cpp
static void test_tier_names_are_device_based() {
    // The registry must parse tiny/small/medium and reject the old names.
    EXPECT(naina::tier_from_string("tiny") == naina::Tier::Tiny);
    EXPECT(naina::tier_from_string("small") == naina::Tier::Small);
    EXPECT(naina::tier_from_string("medium") == naina::Tier::Medium);
    // Unknown strings fall back to Small, the general-purpose default.
    EXPECT(naina::tier_from_string("research") == naina::Tier::Small);
    EXPECT(naina::tier_from_string("") == naina::Tier::Small);
}
```

Then add `test_tier_names_are_device_based();` to the list of calls inside
`main`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_model_loader 2>&1 | tail -20
```

Expected: compile error — `tier_from_string` is not a member of `naina`, and
`Tier::Tiny` does not exist.

- [ ] **Step 3: Change the enum and add the parser**

In `core/include/naina/model_loader.hpp`, replace line 23:

```cpp
enum class Tier { Default, Research };
```

with:

```cpp
// Device tier, not a licence tier. Every OCR model naina ships is
// Apache-2.0; what differs is size and the hardware it suits.
//   Tiny   ~11 MB total — browser, phone, Pi Zero
//   Small  ~54 MB total — laptop, Pi 5, mobile app
//   Medium ~269 MB total — server, desktop
enum class Tier { Tiny, Small, Medium };

// Parse a manifest tier string. Unknown values map to Small so a registry
// written for a newer naina still loads on an older one.
Tier tier_from_string(const std::string& s);

// Inverse of tier_from_string. Returns a static string, never null.
const char* tier_to_string(Tier t);
```

Also change the `ModelEntry` default on line 34 from
`Tier tier = Tier::Default;` to `Tier tier = Tier::Small;`.

In `core/src/model_loader.cc`, add these definitions inside
`namespace naina {`, above the `ModelRegistry::load` definition:

```cpp
Tier tier_from_string(const std::string& s) {
    if (s == "tiny") {
        return Tier::Tiny;
    }
    if (s == "medium") {
        return Tier::Medium;
    }
    return Tier::Small;
}

const char* tier_to_string(Tier t) {
    switch (t) {
        case Tier::Tiny:
            return "tiny";
        case Tier::Medium:
            return "medium";
        case Tier::Small:
            break;
    }
    return "small";
}
```

Then find where `load()` currently reads the tier field. It parses the
string `"default"` / `"research"`. Replace that parsing expression with:

```cpp
        m.tier = tier_from_string(node["tier"] ? node["tier"].as<std::string>() : "small");
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_model_loader 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_model_loader --output-on-failure
```

Expected: build succeeds, `test_model_loader` passes. Other targets still
fail to build — `api.cc` references `Tier::Research`, fixed in Task 4.

- [ ] **Step 5: Commit**

```bash
git add core/include/naina/model_loader.hpp core/src/model_loader.cc core/tests/test_model_loader.cc
git commit -m "Retier registry by device instead of licence

Every PP-OCRv6 and PP-DocLayout model is Apache-2.0, so the
default/research split carries no information. Tiers become
tiny/small/medium, which is what actually varies: 11MB / 54MB / 269MB.
Unknown tier strings degrade to small rather than failing to load."
```

---

## Task 3: Write the OCR model registry

**Files:**
- Modify: `models/registry.yaml` (replace all face entries)
- Modify: `models/manifest.schema.json`
- Test: `core/tests/test_model_loader.cc`

- [ ] **Step 1: Write the failing test**

Append to `core/tests/test_model_loader.cc` before `main`, and add a call to
it in `main`:

```cpp
static void test_ocr_registry_resolves_all_tiers() {
    const auto reg = naina::ModelRegistry::load("models/registry.yaml");

    // Every tier must have det and rec.
    for (auto tier : {naina::Tier::Tiny, naina::Tier::Small, naina::Tier::Medium}) {
        const auto det = reg.resolve("text_detect", tier);
        const auto rec = reg.resolve("text_recognize", tier);
        EXPECT(det.has_value());
        EXPECT(rec.has_value());
        if (det) {
            EXPECT(det->files.count("onnx") == 1);
            EXPECT(det->license == "Apache-2.0");
            EXPECT(det->files.at("onnx").sha256.size() == 64);
            EXPECT(det->files.at("onnx").bytes > 0);
        }
        if (rec) {
            EXPECT(rec->files.count("onnx") == 1);
            EXPECT(rec->files.at("onnx").sha256.size() == 64);
        }
    }

    // Layout exists only for medium until the paddle2onnx export lands.
    EXPECT(reg.resolve("layout_detect", naina::Tier::Medium).has_value());

    // Spot-check one exact hash so a bad copy-paste is caught.
    const auto tiny_det = reg.resolve("text_detect", naina::Tier::Tiny);
    EXPECT(tiny_det.has_value());
    if (tiny_det) {
        EXPECT(tiny_det->files.at("onnx").sha256 ==
               "193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8");
        EXPECT(tiny_det->files.at("onnx").bytes == 1780590);
    }

    // No face models should remain.
    EXPECT(!reg.resolve("face_detect", naina::Tier::Small).has_value());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_model_loader 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_model_loader --output-on-failure 2>&1 | tail -20
```

Expected: FAIL — `text_detect` does not resolve; `face_detect` still does.

- [ ] **Step 3: Write the registry**

Replace the entire `models:` section of `models/registry.yaml` with the
following. Keep the file's existing `schema_version`, `defaults` and header
comment, but delete the `opencv_zoo` line from `defaults` (it was only for
face weights) and add an `hf` base:

```yaml
defaults:
  cache_root: "${NAINA_CACHE:-~/.cache/naina/models}"
  release_base: "https://github.com/jvoltci/naina/releases/download/models-v1"
  # PaddleOCR publishes permissively-licensed ONNX exports on the Hugging
  # Face Hub. ${hf} resolves to this base.
  hf: "https://huggingface.co/PaddlePaddle"

models:

  # ─── Text detection (PP-OCRv6 det, DBNet head) ──────────────────────

  - id: text_detect.tiny
    task: text_detect
    tier: tiny
    arch: pp_ocrv6_det
    license: Apache-2.0
    paper: "PP-OCRv6, arXiv:2606.13108"
    files:
      onnx:
        url:    "${hf}/PP-OCRv6_tiny_det_onnx/resolve/main/inference.onnx"
        sha256: "193bab7a04fca699a6c82e6abb5b81bdb28177f0abd4062552b04908dafb19f8"
        bytes:  1780590
    input:
      shape:  [1, 3, -1, -1]
      pixfmt: bgr
      dtype:  f32
      preprocess:
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.485, 0.456, 0.406]
        std:   [0.229, 0.224, 0.225]
        resize: {mode: limit_max_side, limit: 960, multiple_of: 32}
    output:
      type: db_probability_map
      postprocess:
        thresh:         0.2
        box_thresh:     0.4
        unclip_ratio:   1.4
        max_candidates: 3000

  - id: text_detect.small
    task: text_detect
    tier: small
    arch: pp_ocrv6_det
    license: Apache-2.0
    paper: "PP-OCRv6, arXiv:2606.13108"
    files:
      onnx:
        url:    "${hf}/PP-OCRv6_small_det_onnx/resolve/main/inference.onnx"
        sha256: "d73e0058b7a8086bbd57f3d10b8bcd4ff95363f67e06e2762b5e814fe9c9410e"
        bytes:  9880512
    input:
      shape:  [1, 3, -1, -1]
      pixfmt: bgr
      dtype:  f32
      preprocess:
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.485, 0.456, 0.406]
        std:   [0.229, 0.224, 0.225]
        resize: {mode: limit_max_side, limit: 960, multiple_of: 32}
    output:
      type: db_probability_map
      postprocess:
        thresh:         0.2
        box_thresh:     0.4
        unclip_ratio:   1.4
        max_candidates: 3000

  - id: text_detect.medium
    task: text_detect
    tier: medium
    arch: pp_ocrv6_det
    license: Apache-2.0
    paper: "PP-OCRv6, arXiv:2606.13108"
    files:
      onnx:
        url:    "${hf}/PP-OCRv6_medium_det_onnx/resolve/main/inference.onnx"
        sha256: "eb13b44b25bb36f89528b68720af8a61d9cf381176107f465db1757b65d086e1"
        bytes:  62032837
    input:
      shape:  [1, 3, -1, -1]
      pixfmt: bgr
      dtype:  f32
      preprocess:
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.485, 0.456, 0.406]
        std:   [0.229, 0.224, 0.225]
        resize: {mode: limit_max_side, limit: 960, multiple_of: 32}
    output:
      type: db_probability_map
      postprocess:
        thresh:         0.2
        box_thresh:     0.4
        unclip_ratio:   1.4
        max_candidates: 3000

  # ─── Text recognition (PP-OCRv6 rec, CTC head) ──────────────────────
  #
  # Input height is fixed at 48; width is dynamic. Output is
  # [N, T, 6906] where 6906 = 1 CTC blank at index 0 + 6905 characters.
  # The charset lives beside the weights as charset.txt (see Task 12).

  - id: text_recognize.tiny
    task: text_recognize
    tier: tiny
    arch: pp_ocrv6_rec
    license: Apache-2.0
    paper: "PP-OCRv6, arXiv:2606.13108"
    files:
      onnx:
        url:    "${hf}/PP-OCRv6_tiny_rec_onnx/resolve/main/inference.onnx"
        sha256: "9ef676d6ed3c88256a2d92c640c44f25b0c40947e111b14b8be8f594091563e6"
        bytes:  4462639
    input:
      shape:  [-1, 3, 48, -1]
      pixfmt: bgr
      dtype:  f32
      preprocess:
        height: 48
        max_width: 1200
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.5, 0.5, 0.5]
        std:   [0.5, 0.5, 0.5]
    output:
      type: ctc_logits
      postprocess:
        blank_index: 0
        num_classes: 6906

  - id: text_recognize.small
    task: text_recognize
    tier: small
    arch: pp_ocrv6_rec
    license: Apache-2.0
    paper: "PP-OCRv6, arXiv:2606.13108"
    files:
      onnx:
        url:    "${hf}/PP-OCRv6_small_rec_onnx/resolve/main/inference.onnx"
        sha256: "5435fd747c9e0efe15a96d0b378d5bd157e9492ed8fd80edf08f30d02fa24634"
        bytes:  21159378
    input:
      shape:  [-1, 3, 48, -1]
      pixfmt: bgr
      dtype:  f32
      preprocess:
        height: 48
        max_width: 1200
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.5, 0.5, 0.5]
        std:   [0.5, 0.5, 0.5]
    output:
      type: ctc_logits
      postprocess:
        blank_index: 0
        num_classes: 6906

  - id: text_recognize.medium
    task: text_recognize
    tier: medium
    arch: pp_ocrv6_rec
    license: Apache-2.0
    paper: "PP-OCRv6, arXiv:2606.13108"
    files:
      onnx:
        url:    "${hf}/PP-OCRv6_medium_rec_onnx/resolve/main/inference.onnx"
        sha256: "9c09abf0957f7968c7586464b7397b84ad2387a0497a351af40e9acc71b673ba"
        bytes:  76554979
    input:
      shape:  [-1, 3, 48, -1]
      pixfmt: bgr
      dtype:  f32
      preprocess:
        height: 48
        max_width: 1200
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.5, 0.5, 0.5]
        std:   [0.5, 0.5, 0.5]
    output:
      type: ctc_logits
      postprocess:
        blank_index: 0
        num_classes: 6906

  # ─── Layout analysis (PP-DocLayoutV3) ───────────────────────────────
  #
  # Medium only for now. The 4.8 MB -S and 23.4 MB -M variants ship in
  # Paddle format and need the tools/paddle2onnx_layout.py export (Task 16)
  # before tiny/small can claim layout support.

  - id: layout_detect.medium
    task: layout_detect
    tier: medium
    arch: pp_doclayout_v3
    license: Apache-2.0
    files:
      onnx:
        url:    "${hf}/PP-DocLayoutV3_onnx/resolve/main/inference.onnx"
        sha256: "45bf71750b00739a41fc209f132eb104a4d6b5bb29483c9078164d8b87cf28ba"
        bytes:  130502049
    input:
      shape:  [1, 3, 800, 800]
      pixfmt: rgb
      dtype:  f32
      preprocess:
        scale: [0.00392156862745098, 0.00392156862745098, 0.00392156862745098]
        mean:  [0.485, 0.456, 0.406]
        std:   [0.229, 0.224, 0.225]
        resize: {mode: fixed, target: [800, 800]}
    output:
      type: layout_regions
      postprocess:
        score_thresh: 0.5
        nms_iou:      0.5
```

- [ ] **Step 4: Update the schema**

In `models/manifest.schema.json`, find the `tier` property's `enum` and
replace its value array with:

```json
["tiny", "small", "medium"]
```

Then find the `task` property's `enum` (it lists the face tasks) and replace
its value array with:

```json
["text_detect", "text_recognize", "layout_detect"]
```

If the schema also declares `opencv_zoo` under `defaults`, rename that
property to `hf`.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_model_loader 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_model_loader --output-on-failure
```

Expected: PASS. This test only parses YAML — it does not download anything.

- [ ] **Step 6: Validate the YAML against the schema**

```bash
python3 -m pip install --quiet check-jsonschema pyyaml
python3 - <<'PY'
import json, yaml, jsonschema
schema = json.load(open('models/manifest.schema.json'))
doc = yaml.safe_load(open('models/registry.yaml'))
jsonschema.validate(doc, schema)
print("registry.yaml validates against manifest.schema.json")
PY
```

Expected: the success line, no traceback.

- [ ] **Step 7: Commit**

```bash
git add models/registry.yaml models/manifest.schema.json core/tests/test_model_loader.cc
git commit -m "Registry: 7 OCR models across three device tiers

PP-OCRv6 det+rec at tiny/small/medium plus PP-DocLayoutV3. All
Apache-2.0. Every sha256 verified by downloading and hashing the real
artifact, so a corrupt or substituted download fails closed.

Layout is medium-only until paddle2onnx can export the -S and -M
variants, which ship without ONNX."
```

---

## Task 4: OCR C ABI, and a green build again

After Task 1 the tree does not compile — `api.cc` includes deleted headers.
This task replaces the face ABI with the OCR ABI and stubs every new entry
point so the build and existing tests go green. Real implementations land in
Tasks 9–15.

**Files:**
- Modify: `core/include/naina/naina.h:84-189`
- Modify: `core/src/api.cc:12-14, 225-366`
- Modify: `core/src/api_cpp.cc`
- Test: `core/tests/test_engine_lifecycle.cc`

- [ ] **Step 1: Write the failing test**

Append to `core/tests/test_engine_lifecycle.cc` before `main`, and call it
from `main`:

```cpp
static void test_ocr_surface_exists_and_validates_args() {
    naina_config cfg{};
    cfg.version = 2;
    cfg.backend = NAINA_BACKEND_AUTO;
    cfg.device = NAINA_DEVICE_AUTO;
    cfg.tier = NAINA_TIER_TINY;

    naina_ctx_t* ctx = nullptr;
    const naina_status is = naina_init(&cfg, &ctx);
    EXPECT(is == NAINA_OK);
    if (is != NAINA_OK) {
        return;
    }

    // Null-argument contracts hold before any model is loaded.
    naina_page_t* page = nullptr;
    EXPECT(naina_read(nullptr, nullptr, &page) == NAINA_E_INVALID_ARG);
    EXPECT(naina_read(ctx, nullptr, &page) == NAINA_E_INVALID_ARG);
    EXPECT(naina_read(ctx, nullptr, nullptr) == NAINA_E_INVALID_ARG);

    naina_textbox* boxes = nullptr;
    int32_t nboxes = -1;
    EXPECT(naina_text_detect(ctx, nullptr, &boxes, &nboxes) == NAINA_E_INVALID_ARG);

    naina_region* regions = nullptr;
    int32_t nregions = -1;
    EXPECT(naina_layout_detect(ctx, nullptr, &regions, &nregions) == NAINA_E_INVALID_ARG);

    // Releasing null is a no-op, never a crash.
    naina_page_release(nullptr);
    naina_free_textboxes(nullptr, 0);
    naina_free_regions(nullptr, 0);

    // A config declaring version 1 is still accepted (ABI rule: additive only).
    naina_config old{};
    old.version = 1;
    naina_ctx_t* ctx1 = nullptr;
    EXPECT(naina_init(&old, &ctx1) == NAINA_OK);
    naina_release(ctx1);

    naina_release(ctx);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_engine_lifecycle 2>&1 | tail -20
```

Expected: compile errors — `naina_page_t`, `naina_read`, `naina_textbox`,
`NAINA_TIER_TINY` and `cfg.tier` do not exist. `api.cc` also fails on the
deleted face headers.

- [ ] **Step 3: Replace the ABI surface in `core/include/naina/naina.h`**

Delete the `naina_face` and `naina_person` struct definitions (lines 93–104)
and the entire `Face stack`, `Person stack` and `Tracking` sections
(lines 140–189). Keep `naina_bbox`, `naina_point`, all enums, the opaque
`naina_ctx_t` / `naina_image_t`, lifecycle, and image wrapping exactly as
they are. Delete the `naina_tracker_t` typedef.

Bump the version macros at lines 24–26:

```c
#define NAINA_VERSION_MAJOR 0
#define NAINA_VERSION_MINOR 2
#define NAINA_VERSION_PATCH 0
```

Add this enum after `naina_pixfmt`:

```c
/* Device tier. Selects model size, not licence — every model naina ships
 * is Apache-2.0.
 *   TINY    ~11 MB  — browser, phone, Pi Zero
 *   SMALL   ~54 MB  — laptop, Pi 5, mobile app
 *   MEDIUM  ~269 MB — server, desktop
 * AUTO picks by available memory. */
typedef enum {
    NAINA_TIER_AUTO = 0,
    NAINA_TIER_TINY,
    NAINA_TIER_SMALL,
    NAINA_TIER_MEDIUM,
} naina_tier;

/* Layout region classes emitted by layout_detect. */
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
```

Add these POD types where `naina_face` used to be:

```c
/* A detected text quad. Corners are clockwise from top-left, in SOURCE
 * image coordinates. Quads are not necessarily axis-aligned. */
typedef struct {
    naina_point corners[4];
    float score;
} naina_textbox;

/* A recognised line of text. `text` is UTF-8, NUL-terminated, and owned by
 * the naina_page_t that produced it — it dangles after naina_page_release. */
typedef struct {
    naina_textbox box;
    const char* text;
    float confidence;
    int32_t region_id; /* index into the page's regions, -1 if unassigned */
} naina_textline;

/* A layout region. `order` is the reading-order index within the page. */
typedef struct {
    naina_bbox bbox;
    naina_region_kind kind;
    int32_t order;
} naina_region;
```

Extend the config struct. Per the header's own ABI rules, adding a field
requires a version bump with the old version still accepted:

```c
typedef struct {
    int32_t version; /* 1 = pre-OCR layout; 2 adds `tier` */
    naina_backend backend;
    naina_device device;
    const char* models_root;        /* NULL → $NAINA_CACHE / default */
    int32_t num_threads;            /* 0 = auto */
    int32_t enable_research_models; /* retained for ABI compatibility; ignored */
    naina_tier tier;                /* version >= 2 only */
} naina_config;
```

Add the page handle beside the other opaque handles:

```c
typedef struct naina_page naina_page_t;
```

Add the OCR sections in place of the deleted ones:

```c
/* ─── Reading a page (the primary API) ────────────────────────────── */

/* Run the full pipeline: detect → rectify → recognise → layout → assemble.
 * The returned page owns every string it hands out. Release exactly once. */
NAINA_API naina_status naina_read(naina_ctx_t* ctx,
                                  const naina_image_t* image,
                                  naina_page_t** out_page);
NAINA_API void naina_page_release(naina_page_t* page);

/* Borrowed views into the page. Valid until naina_page_release. */
NAINA_API naina_status naina_page_lines(const naina_page_t* page,
                                        const naina_textline** out_lines,
                                        int32_t* out_count);
NAINA_API naina_status naina_page_regions(const naina_page_t* page,
                                          const naina_region** out_regions,
                                          int32_t* out_count);

/* Serialised views. Borrowed, UTF-8, NUL-terminated. Never null on a
 * successfully-read page; empty string if the page had no text. */
NAINA_API const char* naina_page_markdown(const naina_page_t* page);
NAINA_API const char* naina_page_json(const naina_page_t* page);

/* ─── Stage-level access ──────────────────────────────────────────── */

/* Lib allocates; caller frees with the matching free function. */
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

/* Human-readable name for a region class. Static string, never null. */
NAINA_API const char* naina_region_kind_str(naina_region_kind k);
```

- [ ] **Step 4: Stub the implementations in `core/src/api.cc`**

Replace the three face module includes (lines 12–14) with nothing for now —
the real includes arrive in Task 15.

Replace the entire `Face stack` section (lines 225–337) and the
`Person / tracker stubs` section (lines 339–366) with:

```cpp
// ── OCR surface (implementations land in Tasks 9-15) ─────────────────

extern "C" naina_status naina_read(naina_ctx_t* ctx,
                                   const naina_image_t* image,
                                   naina_page_t** out_page) {
    if (ctx == nullptr || image == nullptr || out_page == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_page = nullptr;
    return NAINA_E_UNSUPPORTED;
}

extern "C" void naina_page_release(naina_page_t*) {}

extern "C" naina_status naina_page_lines(const naina_page_t* page,
                                         const naina_textline** out_lines,
                                         int32_t* out_count) {
    if (page == nullptr || out_lines == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_lines = nullptr;
    *out_count = 0;
    return NAINA_E_UNSUPPORTED;
}

extern "C" naina_status naina_page_regions(const naina_page_t* page,
                                           const naina_region** out_regions,
                                           int32_t* out_count) {
    if (page == nullptr || out_regions == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_regions = nullptr;
    *out_count = 0;
    return NAINA_E_UNSUPPORTED;
}

extern "C" const char* naina_page_markdown(const naina_page_t*) {
    return "";
}

extern "C" const char* naina_page_json(const naina_page_t*) {
    return "";
}

extern "C" naina_status naina_text_detect(naina_ctx_t* ctx,
                                          const naina_image_t* image,
                                          naina_textbox** out_boxes,
                                          int32_t* out_count) {
    if (ctx == nullptr || image == nullptr || out_boxes == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_boxes = nullptr;
    *out_count = 0;
    return NAINA_E_UNSUPPORTED;
}

extern "C" void naina_free_textboxes(naina_textbox* boxes, int32_t /*count*/) {
    std::free(boxes);
}

extern "C" naina_status naina_layout_detect(naina_ctx_t* ctx,
                                            const naina_image_t* image,
                                            naina_region** out_regions,
                                            int32_t* out_count) {
    if (ctx == nullptr || image == nullptr || out_regions == nullptr || out_count == nullptr) {
        return NAINA_E_INVALID_ARG;
    }
    *out_regions = nullptr;
    *out_count = 0;
    return NAINA_E_UNSUPPORTED;
}

extern "C" void naina_free_regions(naina_region* regions, int32_t /*count*/) {
    std::free(regions);
}

extern "C" const char* naina_region_kind_str(naina_region_kind k) {
    switch (k) {
        case NAINA_REGION_TITLE:
            return "title";
        case NAINA_REGION_TEXT:
            return "text";
        case NAINA_REGION_LIST:
            return "list";
        case NAINA_REGION_TABLE:
            return "table";
        case NAINA_REGION_FIGURE:
            return "figure";
        case NAINA_REGION_CAPTION:
            return "caption";
        case NAINA_REGION_FORMULA:
            return "formula";
        case NAINA_REGION_HEADER:
            return "header";
        case NAINA_REGION_FOOTER:
            return "footer";
        case NAINA_REGION_PAGENUM:
            return "pagenum";
        case NAINA_REGION_UNKNOWN:
            break;
    }
    return "unknown";
}
```

Add `#include <cstdlib>` and `#include <cstring>` to the include block if
they are not already present.

- [ ] **Step 5: Teach the context about tiers**

In `struct naina_ctx`, replace the `bool enable_research = false;` field with:

```cpp
    naina::Tier tier = naina::Tier::Small;
```

In `session_for`, replace these two lines:

```cpp
        const naina::Tier tier = enable_research ? naina::Tier::Research : naina::Tier::Default;
        auto entry = registry.resolve(task, tier);
```

with:

```cpp
        auto entry = registry.resolve(task, tier);
        // Tier fallback: a tier without this task degrades to the next
        // larger one rather than failing. Layout is medium-only today.
        if (!entry && tier != naina::Tier::Medium) {
            entry = registry.resolve(task, naina::Tier::Medium);
        }
```

In `naina_init`, replace the `ctx->enable_research = ...` line with:

```cpp
    // `tier` only exists in config version >= 2. Older callers get Small.
    if (cfg != nullptr && cfg->version >= 2) {
        switch (cfg->tier) {
            case NAINA_TIER_TINY:
                ctx->tier = naina::Tier::Tiny;
                break;
            case NAINA_TIER_MEDIUM:
                ctx->tier = naina::Tier::Medium;
                break;
            case NAINA_TIER_SMALL:
            case NAINA_TIER_AUTO:
                ctx->tier = naina::Tier::Small;
                break;
        }
    }
```

- [ ] **Step 6: Fix `core/src/api_cpp.cc`**

Open the file and delete every method that wraps a face or person call
(anything mentioning `naina_face`, `naina_person`, `naina_tracker`, or
`naina_embed_similarity`). Leave the context lifecycle and image wrapping
wrappers intact. Do not add C++ wrappers for the OCR surface yet — that
happens in Task 15 when the behaviour is real.

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --build --preset macos-arm64 2>&1 | tail -10 \
  && ctest --preset macos-arm64 --output-on-failure
```

Expected: the whole project builds and **every** test passes, including the
new `test_ocr_surface_exists_and_validates_args`.

- [ ] **Step 8: Commit**

```bash
git add core/include/naina/naina.h core/src/api.cc core/src/api_cpp.cc core/tests/test_engine_lifecycle.cc
git commit -m "C ABI: replace face surface with OCR surface

naina_read is the primary entry point; naina_page_t owns every string it
returns so bindings need one release call. Stage-level detect/layout
entry points stay available for callers who want the pieces.

Config goes to version 2 to add \`tier\`. Version 1 configs are still
accepted and default to the small tier, per the header's additive-only
ABI rule. Implementations are stubbed NAINA_E_UNSUPPORTED here so the
build is green while the modules land."
```

---

## Task 5: Detection preprocessing — resize to a multiple of 32

The det model takes any `H`/`W` but PaddleOCR clamps the longest side to 960
and rounds both dimensions to a multiple of 32. Getting this wrong shifts
every box, so it is tested on its own before any model runs.

**Files:**
- Modify: `core/src/image_ops.hpp`, `core/src/image_ops.cc`
- Create: `core/tests/test_image_ops_warp.cc`
- Modify: `core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `core/tests/test_image_ops_warp.cc`:

```cpp
// Pure image-op tests: detection resize geometry and quad rectification.
#include "image_ops.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::DetResize;
using naina::internal::ImageView;
using naina::internal::plan_det_resize;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_det_resize_rounds_to_multiple_of_32() {
    // A 1000x500 image: longest side 1000 > 960, so scale = 0.96.
    // 1000*0.96 = 960 (already /32), 500*0.96 = 480 (already /32).
    const DetResize a = plan_det_resize(1000, 500, 960, 32);
    EXPECT(a.out_w == 960);
    EXPECT(a.out_h == 480);
    EXPECT(std::fabs(a.scale_x - 0.96F) < 1e-5F);
    EXPECT(std::fabs(a.scale_y - 0.96F) < 1e-5F);

    // 100x50 is under the limit, so no downscale — but both dims round UP
    // to the next multiple of 32: 100 -> 128, 50 -> 64.
    const DetResize b = plan_det_resize(100, 50, 960, 32);
    EXPECT(b.out_w == 128);
    EXPECT(b.out_h == 64);
    // Scale is per-axis because rounding differs per axis.
    EXPECT(std::fabs(b.scale_x - 1.28F) < 1e-5F);
    EXPECT(std::fabs(b.scale_y - 1.28F) < 1e-5F);

    // Never collapse to zero.
    const DetResize c = plan_det_resize(3, 1, 960, 32);
    EXPECT(c.out_w == 32);
    EXPECT(c.out_h == 32);

    // A very tall image clamps on height.
    const DetResize d = plan_det_resize(200, 4000, 960, 32);
    EXPECT(d.out_h == 960);
    EXPECT(d.out_w % 32 == 0);
    EXPECT(d.out_w > 0);
}

static void test_det_resize_is_idempotent_on_aligned_input() {
    // 640x320 is already aligned and under the limit — nothing should move.
    const DetResize r = plan_det_resize(640, 320, 960, 32);
    EXPECT(r.out_w == 640);
    EXPECT(r.out_h == 320);
    EXPECT(std::fabs(r.scale_x - 1.0F) < 1e-6F);
    EXPECT(std::fabs(r.scale_y - 1.0F) < 1e-6F);
}

int main() {
    test_det_resize_rounds_to_multiple_of_32();
    test_det_resize_is_idempotent_on_aligned_input();
    if (failures == 0) {
        std::printf("test_image_ops_warp: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
```

Register it in `core/tests/CMakeLists.txt` by adding this line after
`naina_add_test(test_engine_lifecycle)`:

```cmake
naina_add_test(test_image_ops_warp)
```

and this line after the existing `target_include_directories(test_sha256 ...)`
line, because the test includes an internal header:

```cmake
target_include_directories(test_image_ops_warp PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --preset macos-arm64 >/dev/null && \
cmake --build --preset macos-arm64 --target test_image_ops_warp 2>&1 | tail -15
```

Expected: compile error — `DetResize` and `plan_det_resize` are undeclared.

- [ ] **Step 3: Add the declaration**

In `core/src/image_ops.hpp`, add inside `namespace naina::internal {`, after
the `Letterbox` struct:

```cpp
// Geometry for the detection resize. PP-OCRv6 det accepts any H/W but
// PaddleOCR clamps the longest side to `limit` and rounds both dimensions
// to a multiple of `multiple_of`. Scale is per-axis because the rounding
// differs per axis.
struct DetResize {
    int32_t out_w;
    int32_t out_h;
    float scale_x;  // out_w / src_w — divide model coords by this to invert
    float scale_y;  // out_h / src_h
};

// Pure geometry; no pixels touched. Exposed for testing.
DetResize plan_det_resize(int32_t src_w, int32_t src_h, int32_t limit, int32_t multiple_of);

// Resize `src` into a `plan.out_w` x `plan.out_h` BGR planar float32 buffer
// with per-channel (x*scale - mean) / std normalisation. `dst` must hold
// 3 * out_w * out_h floats. Bilinear sampling, edge-clamped.
void resize_det_bgr_planar_f32(const ImageView& src,
                               const DetResize& plan,
                               const float scale[3],
                               const float mean[3],
                               const float std_[3],
                               float* dst);
```

- [ ] **Step 4: Implement `plan_det_resize`**

In `core/src/image_ops.cc`, add inside `namespace naina::internal {`:

```cpp
namespace {

// Round `v` to the nearest multiple of `m`, never below `m`.
int32_t round_to_multiple(float v, int32_t m) {
    if (m <= 1) {
        return static_cast<int32_t>(v) > 0 ? static_cast<int32_t>(v) : 1;
    }
    const int32_t n = static_cast<int32_t>(std::lround(v / static_cast<float>(m)));
    return (n < 1 ? 1 : n) * m;
}

}  // namespace

DetResize plan_det_resize(int32_t src_w, int32_t src_h, int32_t limit, int32_t multiple_of) {
    DetResize r{};
    if (src_w <= 0 || src_h <= 0) {
        r.out_w = multiple_of > 0 ? multiple_of : 1;
        r.out_h = r.out_w;
        r.scale_x = 1.0F;
        r.scale_y = 1.0F;
        return r;
    }
    const int32_t longest = src_w > src_h ? src_w : src_h;
    float ratio = 1.0F;
    if (limit > 0 && longest > limit) {
        ratio = static_cast<float>(limit) / static_cast<float>(longest);
    }
    r.out_w = round_to_multiple(static_cast<float>(src_w) * ratio, multiple_of);
    r.out_h = round_to_multiple(static_cast<float>(src_h) * ratio, multiple_of);
    r.scale_x = static_cast<float>(r.out_w) / static_cast<float>(src_w);
    r.scale_y = static_cast<float>(r.out_h) / static_cast<float>(src_h);
    return r;
}
```

Add `#include <cmath>` to the top of `image_ops.cc` if absent.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_image_ops_warp 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_image_ops_warp --output-on-failure
```

Expected: `test_image_ops_warp: all passed`.

- [ ] **Step 6: Commit**

```bash
git add core/src/image_ops.hpp core/src/image_ops.cc core/tests/test_image_ops_warp.cc core/tests/CMakeLists.txt
git commit -m "image_ops: detection resize geometry

PP-OCRv6 det takes any H/W, but PaddleOCR clamps the longest side to 960
and rounds both dims to a multiple of 32. Scale is tracked per-axis
because the rounding differs per axis — using a single scale shifts every
box on non-square input.

Geometry is a pure function so it is tested without pixels or a model."
```

---

## Task 6: Detection resize — the pixel path

**Files:**
- Modify: `core/src/image_ops.cc`
- Modify: `core/tests/test_image_ops_warp.cc`

- [ ] **Step 1: Write the failing test**

Add to `core/tests/test_image_ops_warp.cc` before `main`, and call it from
`main`. Add `#include <cstdint>` and `using naina::internal::resize_det_bgr_planar_f32;`
to the top of the file.

```cpp
// Build a WxH BGR8 image where every pixel's B/G/R = a known function of
// (x, y), so we can assert what a resample must produce.
static std::vector<uint8_t> make_bgr(int w, int h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            px[i + 0] = 10;   // B constant
            px[i + 1] = 128;  // G constant
            px[i + 2] = 250;  // R constant
        }
    }
    return px;
}

static void test_resize_det_normalises_per_channel() {
    const int w = 64;
    const int h = 32;
    auto px = make_bgr(w, h);
    ImageView src{px.data(), w, h, w * 3, NAINA_PIXFMT_BGR8};

    const DetResize plan = plan_det_resize(w, h, 960, 32);
    EXPECT(plan.out_w == 64);
    EXPECT(plan.out_h == 32);

    const float scale[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    const float mean[3] = {0.485F, 0.456F, 0.406F};
    const float sd[3] = {0.229F, 0.224F, 0.225F};

    std::vector<float> dst(static_cast<size_t>(3) * plan.out_w * plan.out_h, -999.0F);
    resize_det_bgr_planar_f32(src, plan, scale, mean, sd, dst.data());

    // Planar layout: channel 0 is the whole first plane.
    const size_t plane = static_cast<size_t>(plan.out_w) * plan.out_h;
    const float want_c0 = (10.0F / 255.0F - mean[0]) / sd[0];
    const float want_c1 = (128.0F / 255.0F - mean[1]) / sd[1];
    const float want_c2 = (250.0F / 255.0F - mean[2]) / sd[2];

    // A constant source must produce a constant output in every plane.
    EXPECT(std::fabs(dst[0] - want_c0) < 1e-4F);
    EXPECT(std::fabs(dst[plane / 2] - want_c0) < 1e-4F);
    EXPECT(std::fabs(dst[plane + 0] - want_c1) < 1e-4F);
    EXPECT(std::fabs(dst[2 * plane + 0] - want_c2) < 1e-4F);

    // Nothing left uninitialised.
    for (float v : dst) {
        EXPECT(v > -900.0F);
    }
}

static void test_resize_det_downscales_dimensions() {
    const int w = 200;
    const int h = 100;
    auto px = make_bgr(w, h);
    ImageView src{px.data(), w, h, w * 3, NAINA_PIXFMT_BGR8};

    // 200x100 -> rounds to 192x96 (nearest multiples of 32).
    const DetResize plan = plan_det_resize(w, h, 960, 32);
    EXPECT(plan.out_w == 192);
    EXPECT(plan.out_h == 96);

    const float scale[3] = {1.0F, 1.0F, 1.0F};
    const float mean[3] = {0.0F, 0.0F, 0.0F};
    const float sd[3] = {1.0F, 1.0F, 1.0F};
    std::vector<float> dst(static_cast<size_t>(3) * plan.out_w * plan.out_h, -999.0F);
    resize_det_bgr_planar_f32(src, plan, scale, mean, sd, dst.data());

    // With identity normalisation the raw channel values survive.
    EXPECT(std::fabs(dst[0] - 10.0F) < 0.5F);
    const size_t plane = static_cast<size_t>(plan.out_w) * plan.out_h;
    EXPECT(std::fabs(dst[plane] - 128.0F) < 0.5F);
    EXPECT(std::fabs(dst[2 * plane] - 250.0F) < 0.5F);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_image_ops_warp 2>&1 | tail -15
```

Expected: link error — `resize_det_bgr_planar_f32` is declared but has no
definition.

- [ ] **Step 3: Implement it**

Add to `core/src/image_ops.cc` inside `namespace naina::internal {`. The
anonymous-namespace helper goes next to `round_to_multiple`:

```cpp
namespace {

// Fetch one channel of one pixel, clamping to the image edge. `ch` is an
// index into the source's native channel order (BGR8 → 0=B, 1=G, 2=R).
float sample_u8(const ImageView& src, int32_t x, int32_t y, int32_t ch) {
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x >= src.width) {
        x = src.width - 1;
    }
    if (y >= src.height) {
        y = src.height - 1;
    }
    const int32_t nch = (src.fmt == NAINA_PIXFMT_GRAY8) ? 1 : 3;
    const size_t off = static_cast<size_t>(y) * static_cast<size_t>(src.stride) +
                       static_cast<size_t>(x) * static_cast<size_t>(nch);
    // GRAY8 replicates its single channel across all three requested planes.
    const int32_t c = (nch == 1) ? 0 : ch;
    return static_cast<float>(src.data[off + static_cast<size_t>(c)]);
}

// Bilinear sample at continuous (fx, fy).
float bilinear_u8(const ImageView& src, float fx, float fy, int32_t ch) {
    const int32_t x0 = static_cast<int32_t>(std::floor(fx));
    const int32_t y0 = static_cast<int32_t>(std::floor(fy));
    const float ax = fx - static_cast<float>(x0);
    const float ay = fy - static_cast<float>(y0);
    const float p00 = sample_u8(src, x0, y0, ch);
    const float p10 = sample_u8(src, x0 + 1, y0, ch);
    const float p01 = sample_u8(src, x0, y0 + 1, ch);
    const float p11 = sample_u8(src, x0 + 1, y0 + 1, ch);
    const float top = p00 + (p10 - p00) * ax;
    const float bot = p01 + (p11 - p01) * ax;
    return top + (bot - top) * ay;
}

}  // namespace

void resize_det_bgr_planar_f32(const ImageView& src,
                               const DetResize& plan,
                               const float scale[3],
                               const float mean[3],
                               const float std_[3],
                               float* dst) {
    if (src.data == nullptr || dst == nullptr || plan.out_w <= 0 || plan.out_h <= 0) {
        return;
    }
    const size_t plane = static_cast<size_t>(plan.out_w) * static_cast<size_t>(plan.out_h);
    // Map destination pixel centres back into source space.
    const float inv_x = 1.0F / (plan.scale_x != 0.0F ? plan.scale_x : 1.0F);
    const float inv_y = 1.0F / (plan.scale_y != 0.0F ? plan.scale_y : 1.0F);

    for (int32_t ch = 0; ch < 3; ++ch) {
        const float s = scale[ch];
        const float m = mean[ch];
        const float d = (std_[ch] != 0.0F) ? std_[ch] : 1.0F;
        float* out = dst + static_cast<size_t>(ch) * plane;
        for (int32_t y = 0; y < plan.out_h; ++y) {
            const float fy = (static_cast<float>(y) + 0.5F) * inv_y - 0.5F;
            for (int32_t x = 0; x < plan.out_w; ++x) {
                const float fx = (static_cast<float>(x) + 0.5F) * inv_x - 0.5F;
                const float raw = bilinear_u8(src, fx, fy, ch);
                out[static_cast<size_t>(y) * plan.out_w + x] = (raw * s - m) / d;
            }
        }
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_image_ops_warp 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_image_ops_warp --output-on-failure
```

Expected: `test_image_ops_warp: all passed`.

- [ ] **Step 5: Commit**

```bash
git add core/src/image_ops.cc core/tests/test_image_ops_warp.cc
git commit -m "image_ops: detection resize pixel path

Bilinear resample to planar BGR float32 with per-channel
(x*scale - mean) / std, matching PP-OCRv6 det's NormalizeImage config
(scale 1/255, mean 0.485/0.456/0.406, std 0.229/0.224/0.225).

Sampling clamps to the image edge rather than padding, so boxes near a
border are not pulled inward. GRAY8 input replicates its single channel."
```

---

## Task 7: Quad rectification for recognition

The rec model needs a `3 x 48 x W` strip per text line. Each detected quad is
perspective-warped to an upright rectangle. PaddleOCR sizes the rectangle from
the quad's own edge lengths and rotates it 90° when it is taller than wide, so
vertical text still reads correctly.

**Files:**
- Modify: `core/src/image_ops.hpp`, `core/src/image_ops.cc`
- Modify: `core/tests/test_image_ops_warp.cc`

- [ ] **Step 1: Write the failing test**

Add to `core/tests/test_image_ops_warp.cc` before `main`, and call from
`main`. Add `using naina::internal::plan_quad_strip;` and
`using naina::internal::QuadStrip;` to the top.

```cpp
static void test_quad_strip_sizing_from_edge_lengths() {
    // An axis-aligned 80x20 quad: 4x wider than tall.
    naina_point q[4];
    q[0] = {10.0F, 10.0F};
    q[1] = {90.0F, 10.0F};
    q[2] = {90.0F, 30.0F};
    q[3] = {10.0F, 30.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.height == 48);
    // aspect 80/20 = 4 -> width = 48*4 = 192
    EXPECT(s.width == 192);
    EXPECT(!s.rotate90);
}

static void test_quad_strip_rotates_tall_quads() {
    // A 20x100 quad: 5x taller than wide, so it is vertical text.
    naina_point q[4];
    q[0] = {0.0F, 0.0F};
    q[1] = {20.0F, 0.0F};
    q[2] = {20.0F, 100.0F};
    q[3] = {0.0F, 100.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.rotate90);
    EXPECT(s.height == 48);
    // After rotation the long side (100) becomes the width: 48*(100/20)=240
    EXPECT(s.width == 240);
}

static void test_quad_strip_clamps_max_width() {
    // An absurdly wide quad must not produce an unbounded tensor.
    naina_point q[4];
    q[0] = {0.0F, 0.0F};
    q[1] = {10000.0F, 0.0F};
    q[2] = {10000.0F, 10.0F};
    q[3] = {0.0F, 10.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.width == 1200);
    EXPECT(s.height == 48);
}

static void test_quad_strip_degenerate_quad_is_safe() {
    naina_point q[4];
    for (auto& p : q) {
        p = {5.0F, 5.0F};
    }
    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    EXPECT(s.width >= 1);
    EXPECT(s.height == 48);
}

static void test_warp_quad_extracts_the_right_pixels() {
    // 100x40 image, left half B=0, right half B=200. Warp the right half
    // and confirm we sampled the bright side.
    const int w = 100;
    const int h = 40;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 3, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 3;
            const uint8_t v = (x >= w / 2) ? 200 : 0;
            px[i + 0] = v;
            px[i + 1] = v;
            px[i + 2] = v;
        }
    }
    ImageView src{px.data(), w, h, w * 3, NAINA_PIXFMT_BGR8};

    naina_point q[4];
    q[0] = {50.0F, 5.0F};
    q[1] = {99.0F, 5.0F};
    q[2] = {99.0F, 35.0F};
    q[3] = {50.0F, 35.0F};

    const QuadStrip s = plan_quad_strip(q, 48, 1200);
    const float scale[3] = {1.0F, 1.0F, 1.0F};
    const float mean[3] = {0.0F, 0.0F, 0.0F};
    const float sd[3] = {1.0F, 1.0F, 1.0F};
    std::vector<float> dst(static_cast<size_t>(3) * s.width * s.height, -999.0F);
    naina::internal::warp_quad_bgr_planar_f32(src, q, s, scale, mean, sd, dst.data());

    // Every sample came from the bright half.
    const size_t plane = static_cast<size_t>(s.width) * s.height;
    for (size_t i = 0; i < plane; ++i) {
        EXPECT(dst[i] > 150.0F);
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_image_ops_warp 2>&1 | tail -15
```

Expected: compile error — `QuadStrip`, `plan_quad_strip` and
`warp_quad_bgr_planar_f32` are undeclared.

- [ ] **Step 3: Add the declarations**

In `core/src/image_ops.hpp`, after the `DetResize` block:

```cpp
// Target geometry for one rectified text strip. The recogniser's input
// height is fixed (48 for PP-OCRv6 rec); width follows the quad's aspect
// ratio, clamped so a pathological box cannot allocate an unbounded tensor.
struct QuadStrip {
    int32_t width;
    int32_t height;
    bool rotate90;  // true when the quad is taller than wide (vertical text)
};

// Pure geometry. `corners` is clockwise from top-left.
QuadStrip plan_quad_strip(const naina_point corners[4], int32_t height, int32_t max_width);

// Perspective-warp the quad out of `src` into a `plan.width` x `plan.height`
// BGR planar float32 buffer with per-channel (x*scale - mean) / std.
// `dst` must hold 3 * plan.width * plan.height floats.
void warp_quad_bgr_planar_f32(const ImageView& src,
                              const naina_point corners[4],
                              const QuadStrip& plan,
                              const float scale[3],
                              const float mean[3],
                              const float std_[3],
                              float* dst);
```

- [ ] **Step 4: Implement both functions**

Add to `core/src/image_ops.cc` inside `namespace naina::internal {`:

```cpp
namespace {

float dist(const naina_point& a, const naina_point& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

QuadStrip plan_quad_strip(const naina_point corners[4], int32_t height, int32_t max_width) {
    QuadStrip s{};
    s.height = height > 0 ? height : 48;

    // PaddleOCR's get_rotate_crop_image: take the longer of each opposing
    // edge pair, so a slightly skewed quad is not undersized.
    const float top = dist(corners[0], corners[1]);
    const float bottom = dist(corners[3], corners[2]);
    const float left = dist(corners[0], corners[3]);
    const float right = dist(corners[1], corners[2]);
    float quad_w = top > bottom ? top : bottom;
    float quad_h = left > right ? left : right;

    // A quad markedly taller than wide is vertical text: rotate so the
    // reading direction runs along the strip's width.
    s.rotate90 = (quad_w > 0.0F) && (quad_h / quad_w >= 1.5F);
    if (s.rotate90) {
        const float t = quad_w;
        quad_w = quad_h;
        quad_h = t;
    }

    if (quad_h <= 0.0F || quad_w <= 0.0F) {
        s.width = 1;
        return s;
    }
    const float aspect = quad_w / quad_h;
    int32_t w = static_cast<int32_t>(std::lround(static_cast<float>(s.height) * aspect));
    if (w < 1) {
        w = 1;
    }
    if (max_width > 0 && w > max_width) {
        w = max_width;
    }
    s.width = w;
    return s;
}

void warp_quad_bgr_planar_f32(const ImageView& src,
                              const naina_point corners[4],
                              const QuadStrip& plan,
                              const float scale[3],
                              const float mean[3],
                              const float std_[3],
                              float* dst) {
    if (src.data == nullptr || dst == nullptr || plan.width <= 0 || plan.height <= 0) {
        return;
    }
    // Order the quad so corner 0 maps to the strip's top-left. When the quad
    // is vertical we start from corner 3, which rotates the sampling frame
    // by 90 degrees without a second pass over the pixels.
    naina_point c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = corners[plan.rotate90 ? ((i + 3) % 4) : i];
    }

    const size_t plane = static_cast<size_t>(plan.width) * static_cast<size_t>(plan.height);
    const float fw = static_cast<float>(plan.width);
    const float fh = static_cast<float>(plan.height);

    for (int32_t y = 0; y < plan.height; ++y) {
        // v, u in [0,1] across the strip; bilinear blend of the four corners
        // is the exact inverse map for a planar quad.
        const float v = (static_cast<float>(y) + 0.5F) / fh;
        for (int32_t x = 0; x < plan.width; ++x) {
            const float u = (static_cast<float>(x) + 0.5F) / fw;
            const float top_x = c[0].x + (c[1].x - c[0].x) * u;
            const float top_y = c[0].y + (c[1].y - c[0].y) * u;
            const float bot_x = c[3].x + (c[2].x - c[3].x) * u;
            const float bot_y = c[3].y + (c[2].y - c[3].y) * u;
            const float sx = top_x + (bot_x - top_x) * v;
            const float sy = top_y + (bot_y - top_y) * v;

            for (int32_t ch = 0; ch < 3; ++ch) {
                const float d = (std_[ch] != 0.0F) ? std_[ch] : 1.0F;
                const float raw = bilinear_u8(src, sx, sy, ch);
                dst[static_cast<size_t>(ch) * plane +
                    static_cast<size_t>(y) * plan.width + x] =
                    (raw * scale[ch] - mean[ch]) / d;
            }
        }
    }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset macos-arm64 --target test_image_ops_warp 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_image_ops_warp --output-on-failure
```

Expected: `test_image_ops_warp: all passed`.

- [ ] **Step 6: Commit**

```bash
git add core/src/image_ops.hpp core/src/image_ops.cc core/tests/test_image_ops_warp.cc
git commit -m "image_ops: quad rectification to 48px recognition strips

Strip width follows the quad's aspect ratio using the longer of each
opposing edge pair, so a skewed box is not undersized. Quads at least 1.5x
taller than wide are treated as vertical text and rotated, which is what
PaddleOCR's get_rotate_crop_image does.

Width is clamped (default 1200) so one pathological detection cannot
allocate an unbounded tensor. Bilinear corner blending is the exact
inverse map for a planar quad, so no homography solve is needed."
```

---

## Task 8: Polygon geometry — convex hull and minimum-area rectangle

DBNet decoding needs, per connected blob: the convex hull of its border
pixels, then the minimum-area enclosing rectangle. PaddleOCR gets these from
OpenCV; naina has no OpenCV dependency and will not add one, so both are
implemented here as pure functions.

**Files:**
- Create: `core/src/geometry.hpp`, `core/src/geometry.cc`
- Create: `core/tests/test_geometry.cc`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `core/tests/test_geometry.cc`:

```cpp
// Pure polygon geometry: convex hull, minimum-area rectangle, unclip.
#include "geometry.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::convex_hull;
using naina::internal::min_area_quad;
using naina::internal::polygon_area;
using naina::internal::polygon_perimeter;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_hull_of_a_square_with_interior_points() {
    std::vector<naina_point> pts = {
        {0, 0}, {10, 0}, {10, 10}, {0, 10},  // corners
        {5, 5}, {3, 7},  {8, 2},             // interior, must be dropped
    };
    const auto hull = convex_hull(pts);
    EXPECT(hull.size() == 4);
    // Hull area equals the square's area.
    EXPECT(std::fabs(polygon_area(hull) - 100.0F) < 1e-3F);
}

static void test_hull_handles_collinear_and_duplicate_points() {
    std::vector<naina_point> pts = {
        {0, 0}, {5, 0}, {10, 0},  // collinear along the bottom
        {10, 10}, {0, 10}, {0, 0} // duplicate of the first
    };
    const auto hull = convex_hull(pts);
    // Collinear interior points are not vertices.
    EXPECT(hull.size() == 4);
    EXPECT(std::fabs(polygon_area(hull) - 100.0F) < 1e-3F);
}

static void test_hull_degenerate_inputs() {
    EXPECT(convex_hull({}).empty());
    const auto one = convex_hull({{3, 4}});
    EXPECT(one.size() == 1);
    const auto two = convex_hull({{0, 0}, {1, 1}});
    EXPECT(two.size() == 2);
}

static void test_min_area_quad_of_axis_aligned_box() {
    std::vector<naina_point> pts = {{2, 3}, {12, 3}, {12, 9}, {2, 9}};
    naina_point out[4];
    EXPECT(min_area_quad(pts, out));
    // Area must be 10*6 = 60 regardless of corner ordering.
    std::vector<naina_point> q(out, out + 4);
    EXPECT(std::fabs(polygon_area(q) - 60.0F) < 1e-2F);
    // Every input point lies on or inside the quad's bounding extent.
    float minx = out[0].x, maxx = out[0].x, miny = out[0].y, maxy = out[0].y;
    for (int i = 1; i < 4; ++i) {
        minx = out[i].x < minx ? out[i].x : minx;
        maxx = out[i].x > maxx ? out[i].x : maxx;
        miny = out[i].y < miny ? out[i].y : miny;
        maxy = out[i].y > maxy ? out[i].y : maxy;
    }
    EXPECT(minx <= 2.01F && maxx >= 11.99F);
    EXPECT(miny <= 3.01F && maxy >= 8.99F);
}

static void test_min_area_quad_beats_bounding_box_when_rotated() {
    // A 45-degree diamond. Its axis-aligned bbox has area 200; the true
    // minimum-area rectangle is the rotated square of area 100.
    std::vector<naina_point> pts = {{10, 0}, {20, 10}, {10, 20}, {0, 10}};
    naina_point out[4];
    EXPECT(min_area_quad(pts, out));
    std::vector<naina_point> q(out, out + 4);
    const float area = polygon_area(q);
    EXPECT(area < 150.0F);  // strictly better than the 200 bbox
    EXPECT(std::fabs(area - 200.0F) > 1.0F);
}

static void test_min_area_quad_rejects_too_few_points() {
    naina_point out[4];
    EXPECT(!min_area_quad({}, out));
    EXPECT(!min_area_quad({{0, 0}, {1, 1}}, out));
}

static void test_perimeter_and_area() {
    std::vector<naina_point> sq = {{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    EXPECT(std::fabs(polygon_area(sq) - 16.0F) < 1e-3F);
    EXPECT(std::fabs(polygon_perimeter(sq) - 16.0F) < 1e-3F);
    // Area is unsigned: reversing the winding must not flip the sign.
    std::vector<naina_point> rev = {{0, 4}, {4, 4}, {4, 0}, {0, 0}};
    EXPECT(std::fabs(polygon_area(rev) - 16.0F) < 1e-3F);
}

int main() {
    test_hull_of_a_square_with_interior_points();
    test_hull_handles_collinear_and_duplicate_points();
    test_hull_degenerate_inputs();
    test_min_area_quad_of_axis_aligned_box();
    test_min_area_quad_beats_bounding_box_when_rotated();
    test_min_area_quad_rejects_too_few_points();
    test_perimeter_and_area();
    if (failures == 0) {
        std::printf("test_geometry: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
```

Register it in `core/tests/CMakeLists.txt`:

```cmake
naina_add_test(test_geometry)
target_include_directories(test_geometry PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --preset macos-arm64 >/dev/null && \
cmake --build --preset macos-arm64 --target test_geometry 2>&1 | tail -10
```

Expected: fatal error — `geometry.hpp` not found.

- [ ] **Step 3: Write the header**

Create `core/src/geometry.hpp`:

```cpp
// Pure 2D polygon geometry for detection post-processing.
//
// PaddleOCR leans on OpenCV for these. naina has no OpenCV dependency and
// will not take one — the whole point is a small portable core — so the
// handful of primitives actually needed live here.
#ifndef NAINA_INTERNAL_GEOMETRY_HPP
#define NAINA_INTERNAL_GEOMETRY_HPP

#include "naina/naina.h"

#include <vector>

namespace naina::internal {

// Unsigned area via the shoelace formula. Winding-order independent.
float polygon_area(const std::vector<naina_point>& poly);

// Closed-path perimeter (includes the last→first edge).
float polygon_perimeter(const std::vector<naina_point>& poly);

// Convex hull, counter-clockwise, no collinear interior vertices.
// Andrew's monotone chain: O(n log n). Inputs of size < 3 are returned
// deduplicated and unchanged in spirit.
std::vector<naina_point> convex_hull(std::vector<naina_point> pts);

// Minimum-area enclosing rectangle via rotating calipers over the hull.
// Writes 4 corners to `out` in consistent winding. Returns false when
// fewer than 3 distinct points are supplied.
bool min_area_quad(const std::vector<naina_point>& pts, naina_point out[4]);

// Expand a convex polygon outward so each edge moves `distance` along its
// outward normal, then re-intersect adjacent edges. This is what DBNet's
// "unclip" step does; for a convex polygon it is exactly equivalent to a
// Clipper/pyclipper offset, without the dependency.
std::vector<naina_point> offset_convex_polygon(const std::vector<naina_point>& poly,
                                               float distance);

}  // namespace naina::internal

#endif  // NAINA_INTERNAL_GEOMETRY_HPP
```

- [ ] **Step 4: Implement hull, area, perimeter and minimum-area rectangle**

Create `core/src/geometry.cc`:

```cpp
#include "geometry.hpp"

#include <algorithm>
#include <cmath>

namespace naina::internal {

namespace {

// Cross product of (o->a) x (o->b). Positive when o,a,b turn left.
float cross(const naina_point& o, const naina_point& a, const naina_point& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

}  // namespace

float polygon_area(const std::vector<naina_point>& poly) {
    if (poly.size() < 3) {
        return 0.0F;
    }
    float acc = 0.0F;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        acc += p.x * q.y - q.x * p.y;
    }
    return std::fabs(acc) * 0.5F;
}

float polygon_perimeter(const std::vector<naina_point>& poly) {
    if (poly.size() < 2) {
        return 0.0F;
    }
    float acc = 0.0F;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        const float dx = q.x - p.x;
        const float dy = q.y - p.y;
        acc += std::sqrt(dx * dx + dy * dy);
    }
    return acc;
}

std::vector<naina_point> convex_hull(std::vector<naina_point> pts) {
    std::sort(pts.begin(), pts.end(), [](const naina_point& a, const naina_point& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    pts.erase(std::unique(pts.begin(), pts.end(),
                          [](const naina_point& a, const naina_point& b) {
                              return a.x == b.x && a.y == b.y;
                          }),
              pts.end());
    const size_t n = pts.size();
    if (n < 3) {
        return pts;
    }

    std::vector<naina_point> hull(2 * n);
    size_t k = 0;
    // Lower hull, then upper hull. `<= 0` drops collinear points.
    for (size_t i = 0; i < n; ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0.0F) {
            --k;
        }
        hull[k++] = pts[i];
    }
    for (size_t i = n - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0.0F) {
            --k;
        }
        hull[k++] = pts[i - 1];
    }
    hull.resize(k - 1);  // last point repeats the first
    return hull;
}

bool min_area_quad(const std::vector<naina_point>& pts, naina_point out[4]) {
    if (out == nullptr) {
        return false;
    }
    const std::vector<naina_point> hull = convex_hull(pts);
    if (hull.size() < 3) {
        return false;
    }

    float best_area = -1.0F;
    float best_ux = 1.0F;
    float best_uy = 0.0F;
    float best_min_u = 0.0F;
    float best_max_u = 0.0F;
    float best_min_v = 0.0F;
    float best_max_v = 0.0F;

    // Rotating calipers: the minimum-area rectangle always has one side
    // flush with a hull edge, so testing every edge direction is exact.
    const size_t n = hull.size();
    for (size_t i = 0; i < n; ++i) {
        const naina_point& a = hull[i];
        const naina_point& b = hull[(i + 1) % n];
        float ex = b.x - a.x;
        float ey = b.y - a.y;
        const float len = std::sqrt(ex * ex + ey * ey);
        if (len < 1e-6F) {
            continue;
        }
        ex /= len;
        ey /= len;
        // Project every hull vertex onto the edge direction and its normal.
        float min_u = 1e30F;
        float max_u = -1e30F;
        float min_v = 1e30F;
        float max_v = -1e30F;
        for (const auto& p : hull) {
            const float u = p.x * ex + p.y * ey;
            const float v = -p.x * ey + p.y * ex;
            min_u = std::min(min_u, u);
            max_u = std::max(max_u, u);
            min_v = std::min(min_v, v);
            max_v = std::max(max_v, v);
        }
        const float area = (max_u - min_u) * (max_v - min_v);
        if (best_area < 0.0F || area < best_area) {
            best_area = area;
            best_ux = ex;
            best_uy = ey;
            best_min_u = min_u;
            best_max_u = max_u;
            best_min_v = min_v;
            best_max_v = max_v;
        }
    }
    if (best_area < 0.0F) {
        return false;
    }

    // Rebuild world-space corners from (u, v) extents.
    auto to_world = [&](float u, float v) {
        naina_point p{};
        p.x = u * best_ux - v * best_uy;
        p.y = u * best_uy + v * best_ux;
        return p;
    };
    out[0] = to_world(best_min_u, best_min_v);
    out[1] = to_world(best_max_u, best_min_v);
    out[2] = to_world(best_max_u, best_max_v);
    out[3] = to_world(best_min_u, best_max_v);
    return true;
}

}  // namespace naina::internal
```

Add `src/geometry.cc` to `NAINA_SOURCES` in `core/CMakeLists.txt`, directly
after `src/image_ops.cc`.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_geometry 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_geometry --output-on-failure
```

Expected: `test_geometry: all passed`. Note `offset_convex_polygon` is
declared but not yet defined — that is fine because no test links against it
yet. Task 9 defines it.

- [ ] **Step 6: Commit**

```bash
git add core/src/geometry.hpp core/src/geometry.cc core/tests/test_geometry.cc core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "geometry: convex hull and minimum-area rectangle

DBNet decoding needs both per detected blob. PaddleOCR gets them from
OpenCV; naina takes no OpenCV dependency, so monotone-chain hull plus
rotating calipers live here as pure functions.

Rotating calipers is exact, not approximate: the minimum-area rectangle
always has a side flush with a hull edge, so testing every edge direction
finds the true optimum. A rotated diamond yields area 100 rather than the
200 an axis-aligned bounding box would give."
```

---

## Task 9: Unclip — expand a shrunk DBNet polygon

DBNet is trained on *shrunk* text polygons, so every decoded box must be
expanded back out. PaddleOCR uses pyclipper with
`distance = area * unclip_ratio / perimeter`. For a convex polygon that
offset is exactly "move each edge `distance` along its outward normal, then
re-intersect adjacent edges", which needs no dependency.

**Files:**
- Modify: `core/src/geometry.cc`
- Modify: `core/tests/test_geometry.cc`

- [ ] **Step 1: Write the failing test**

Add to `core/tests/test_geometry.cc` before `main`, and call from `main`. Add
`using naina::internal::offset_convex_polygon;` at the top.

```cpp
static void test_offset_grows_a_square_by_distance_on_every_side() {
    std::vector<naina_point> sq = {{10, 10}, {20, 10}, {20, 20}, {10, 20}};
    const auto out = offset_convex_polygon(sq, 2.0F);
    EXPECT(out.size() == 4);
    // A square offset by d becomes a square with each side 2d longer.
    EXPECT(std::fabs(polygon_area(out) - 196.0F) < 0.5F);  // 14 * 14
    float minx = 1e9F, maxx = -1e9F, miny = 1e9F, maxy = -1e9F;
    for (const auto& p : out) {
        minx = p.x < minx ? p.x : minx;
        maxx = p.x > maxx ? p.x : maxx;
        miny = p.y < miny ? p.y : miny;
        maxy = p.y > maxy ? p.y : maxy;
    }
    EXPECT(std::fabs(minx - 8.0F) < 0.1F);
    EXPECT(std::fabs(maxx - 22.0F) < 0.1F);
    EXPECT(std::fabs(miny - 8.0F) < 0.1F);
    EXPECT(std::fabs(maxy - 22.0F) < 0.1F);
}

static void test_offset_is_winding_order_independent() {
    std::vector<naina_point> ccw = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    std::vector<naina_point> cw = {{0, 10}, {10, 10}, {10, 0}, {0, 0}};
    const float a = polygon_area(offset_convex_polygon(ccw, 3.0F));
    const float b = polygon_area(offset_convex_polygon(cw, 3.0F));
    // Both must GROW, not one grow and one shrink.
    EXPECT(a > 100.0F);
    EXPECT(b > 100.0F);
    EXPECT(std::fabs(a - b) < 0.5F);
}

static void test_offset_zero_distance_is_identity() {
    std::vector<naina_point> sq = {{1, 1}, {5, 1}, {5, 5}, {1, 5}};
    const auto out = offset_convex_polygon(sq, 0.0F);
    EXPECT(std::fabs(polygon_area(out) - 16.0F) < 1e-3F);
}

static void test_offset_degenerate_input_is_safe() {
    EXPECT(offset_convex_polygon({}, 5.0F).empty());
    const auto two = offset_convex_polygon({{0, 0}, {1, 1}}, 5.0F);
    EXPECT(two.size() == 2);  // returned unchanged, not corrupted
}

static void test_db_unclip_distance_formula() {
    // A 10x4 box: area 40, perimeter 28, ratio 1.4
    //   distance = 40 * 1.4 / 28 = 2.0
    std::vector<naina_point> box = {{0, 0}, {10, 0}, {10, 4}, {0, 4}};
    const float area = polygon_area(box);
    const float per = polygon_perimeter(box);
    EXPECT(std::fabs(area - 40.0F) < 1e-3F);
    EXPECT(std::fabs(per - 28.0F) < 1e-3F);
    const float d = area * 1.4F / per;
    EXPECT(std::fabs(d - 2.0F) < 1e-3F);
    const auto grown = offset_convex_polygon(box, d);
    // 14 x 8 = 112
    EXPECT(std::fabs(polygon_area(grown) - 112.0F) < 1.0F);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_geometry 2>&1 | tail -10
```

Expected: link error — undefined symbol `offset_convex_polygon`.

- [ ] **Step 3: Implement it**

Add to `core/src/geometry.cc` inside `namespace naina::internal {`:

```cpp
std::vector<naina_point> offset_convex_polygon(const std::vector<naina_point>& poly,
                                               float distance) {
    if (poly.size() < 3 || distance == 0.0F) {
        return poly;
    }
    const size_t n = poly.size();

    // Signed area tells us the winding, which fixes which normal points out.
    float signed2 = 0.0F;
    for (size_t i = 0; i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        signed2 += p.x * q.y - q.x * p.y;
    }
    // For counter-clockwise winding (signed2 > 0) the outward normal of edge
    // p->q is (dy, -dx); for clockwise it is the negation.
    const float sign = (signed2 > 0.0F) ? 1.0F : -1.0F;

    // Each edge becomes a line offset outward by `distance`, stored as a
    // point plus direction. Adjacent offset lines are then intersected.
    struct Line {
        float px, py, dx, dy;
    };
    std::vector<Line> lines(n);
    for (size_t i = 0; i < n; ++i) {
        const naina_point& p = poly[i];
        const naina_point& q = poly[(i + 1) % n];
        float dx = q.x - p.x;
        float dy = q.y - p.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9F) {
            // Zero-length edge: keep the vertex, no meaningful normal.
            lines[i] = Line{p.x, p.y, 1.0F, 0.0F};
            continue;
        }
        dx /= len;
        dy /= len;
        const float nx = sign * dy;
        const float ny = -sign * dx;
        lines[i] = Line{p.x + nx * distance, p.y + ny * distance, dx, dy};
    }

    std::vector<naina_point> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const Line& a = lines[(i + n - 1) % n];
        const Line& b = lines[i];
        // Solve a.p + t*a.d == b.p + s*b.d for t.
        const float denom = a.dx * b.dy - a.dy * b.dx;
        if (std::fabs(denom) < 1e-9F) {
            // Parallel adjacent edges (straight line): use the offset vertex.
            out.push_back(naina_point{b.px, b.py});
            continue;
        }
        const float t = ((b.px - a.px) * b.dy - (b.py - a.py) * b.dx) / denom;
        out.push_back(naina_point{a.px + a.dx * t, a.py + a.dy * t});
    }
    return out;
}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_geometry 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_geometry --output-on-failure
```

Expected: `test_geometry: all passed`.

- [ ] **Step 5: Commit**

```bash
git add core/src/geometry.cc core/tests/test_geometry.cc
git commit -m "geometry: convex polygon offset for DBNet unclip

DBNet trains on shrunk polygons, so decoded boxes must be expanded by
distance = area * unclip_ratio / perimeter. PaddleOCR calls pyclipper for
this; for a convex polygon the same result comes from offsetting each edge
along its outward normal and re-intersecting adjacent edges.

Winding order is derived from the signed area, so a clockwise polygon
grows rather than collapsing inward — the failure mode that makes every
box shrink to nothing."
```

---

## Task 10: Border tracing on the binary map

DBNet's probability map is thresholded into a bitmap; each connected
foreground blob becomes one text box. PaddleOCR calls
`cv2.findContours(..., RETR_LIST, CHAIN_APPROX_SIMPLE)`. naina needs the
border pixels of each blob, which is Moore-neighbour tracing plus a
flood-fill to mark blobs already visited.

**Files:**
- Create: `core/src/modules/db_postprocess.hpp`, `core/src/modules/db_postprocess.cc`
- Create: `core/tests/test_db_postprocess.cc`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `core/tests/test_db_postprocess.cc`:

```cpp
// DBNet post-processing: bitmap -> blob borders -> quads.
#include "modules/db_postprocess.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using naina::internal::db_postprocess::Bitmap;
using naina::internal::db_postprocess::find_blob_borders;

static int failures = 0;

#define EXPECT(cond)                                                             \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// Build a bitmap from an ASCII picture: '#' is foreground.
static Bitmap from_ascii(const std::vector<const char*>& rows) {
    Bitmap bm;
    bm.height = static_cast<int32_t>(rows.size());
    bm.width = 0;
    for (const char* r : rows) {
        int32_t len = 0;
        while (r[len] != '\0') {
            ++len;
        }
        if (len > bm.width) {
            bm.width = len;
        }
    }
    bm.px.assign(static_cast<size_t>(bm.width) * bm.height, 0);
    for (int32_t y = 0; y < bm.height; ++y) {
        const char* r = rows[static_cast<size_t>(y)];
        for (int32_t x = 0; r[x] != '\0'; ++x) {
            if (r[x] == '#') {
                bm.px[static_cast<size_t>(y) * bm.width + x] = 1;
            }
        }
    }
    return bm;
}

static void test_single_rectangle_blob() {
    const Bitmap bm = from_ascii({
        "........",
        ".####...",
        ".####...",
        "........",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 1);
    if (blobs.empty()) {
        return;
    }
    // Every border pixel must be inside the 4x2 block at x in [1,4], y in [1,2].
    for (const auto& p : blobs[0]) {
        EXPECT(p.x >= 1.0F && p.x <= 4.0F);
        EXPECT(p.y >= 1.0F && p.y <= 2.0F);
    }
    // A 4x2 block is all border: 8 pixels.
    EXPECT(blobs[0].size() >= 6);
}

static void test_two_separate_blobs() {
    const Bitmap bm = from_ascii({
        "##...##",
        "##...##",
        ".......",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 2);
}

static void test_diagonally_touching_blobs_are_one() {
    // 8-connectivity: these two squares touch at a corner and count as one.
    const Bitmap bm = from_ascii({
        "##...",
        "##...",
        "..##.",
        "..##.",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 1);
}

static void test_single_pixel_blob() {
    const Bitmap bm = from_ascii({
        ".....",
        "..#..",
        ".....",
    });
    const auto blobs = find_blob_borders(bm, 3000);
    EXPECT(blobs.size() == 1);
    if (!blobs.empty()) {
        EXPECT(blobs[0].size() == 1);
    }
}

static void test_empty_bitmap_yields_nothing() {
    const Bitmap bm = from_ascii({"....", "....."});
    EXPECT(find_blob_borders(bm, 3000).empty());
}

static void test_max_candidates_caps_output() {
    // A checkerboard of isolated pixels: many blobs, capped at 3.
    Bitmap bm;
    bm.width = 20;
    bm.height = 20;
    bm.px.assign(400, 0);
    for (int32_t y = 0; y < 20; y += 2) {
        for (int32_t x = 0; x < 20; x += 2) {
            bm.px[static_cast<size_t>(y) * 20 + x] = 1;
        }
    }
    const auto blobs = find_blob_borders(bm, 3);
    EXPECT(blobs.size() == 3);
}

int main() {
    test_single_rectangle_blob();
    test_two_separate_blobs();
    test_diagonally_touching_blobs_are_one();
    test_single_pixel_blob();
    test_empty_bitmap_yields_nothing();
    test_max_candidates_caps_output();
    if (failures == 0) {
        std::printf("test_db_postprocess: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
```

Register in `core/tests/CMakeLists.txt`:

```cmake
naina_add_test(test_db_postprocess)
target_include_directories(test_db_postprocess PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --preset macos-arm64 >/dev/null && \
cmake --build --preset macos-arm64 --target test_db_postprocess 2>&1 | tail -10
```

Expected: fatal error — `modules/db_postprocess.hpp` not found.

- [ ] **Step 3: Write the header**

Create `core/src/modules/db_postprocess.hpp`:

```cpp
// DBNet post-processing: probability map -> text quads.
//
// Pure functions, no session and no model. The detection module calls this
// after inference; keeping it separate means the whole decode path is
// testable from an ASCII picture.
#ifndef NAINA_INTERNAL_DB_POSTPROCESS_HPP
#define NAINA_INTERNAL_DB_POSTPROCESS_HPP

#include "naina/naina.h"

#include <cstdint>
#include <vector>

namespace naina::internal::db_postprocess {

// A binary foreground mask. `px` is width*height, values 0 or 1.
struct Bitmap {
    int32_t width = 0;
    int32_t height = 0;
    std::vector<uint8_t> px;
};

struct Config {
    float thresh = 0.2F;          // probability -> foreground
    float box_thresh = 0.4F;      // mean probability inside a box to keep it
    float unclip_ratio = 1.4F;    // DBNet shrink compensation
    int32_t max_candidates = 3000;
    float min_box_side = 3.0F;    // discard boxes thinner than this
};

// Threshold a probability map into a Bitmap. `prob` is height*width row-major.
Bitmap binarize(const float* prob, int32_t width, int32_t height, float thresh);

// Border pixels of each 8-connected foreground blob, at most
// `max_candidates` blobs. Blob order follows a raster scan of seed pixels,
// which makes the output deterministic.
std::vector<std::vector<naina_point>> find_blob_borders(const Bitmap& bm,
                                                        int32_t max_candidates);

// Mean probability inside a quad, sampled over its axis-aligned extent.
// This is PaddleOCR's box_score_fast.
float box_score(const float* prob,
                int32_t width,
                int32_t height,
                const naina_point quad[4]);

// Full decode: probability map -> quads in probability-map coordinates.
// Callers scale the result back to source coordinates.
std::vector<naina_textbox> decode(const float* prob,
                                  int32_t width,
                                  int32_t height,
                                  const Config& cfg);

}  // namespace naina::internal::db_postprocess

#endif  // NAINA_INTERNAL_DB_POSTPROCESS_HPP
```

- [ ] **Step 4: Implement `binarize` and `find_blob_borders`**

Create `core/src/modules/db_postprocess.cc`:

```cpp
#include "db_postprocess.hpp"

#include "../geometry.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace naina::internal::db_postprocess {

Bitmap binarize(const float* prob, int32_t width, int32_t height, float thresh) {
    Bitmap bm;
    bm.width = width;
    bm.height = height;
    if (prob == nullptr || width <= 0 || height <= 0) {
        bm.width = 0;
        bm.height = 0;
        return bm;
    }
    const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
    bm.px.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        bm.px[i] = prob[i] > thresh ? 1 : 0;
    }
    return bm;
}

std::vector<std::vector<naina_point>> find_blob_borders(const Bitmap& bm,
                                                        int32_t max_candidates) {
    std::vector<std::vector<naina_point>> blobs;
    if (bm.width <= 0 || bm.height <= 0 || bm.px.empty()) {
        return blobs;
    }
    const int32_t w = bm.width;
    const int32_t h = bm.height;
    std::vector<uint8_t> seen(bm.px.size(), 0);

    auto fg = [&](int32_t x, int32_t y) {
        return x >= 0 && y >= 0 && x < w && y < h &&
               bm.px[static_cast<size_t>(y) * w + x] != 0;
    };

    // 8-connected neighbour offsets.
    static const int32_t dx8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int32_t dy8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

    std::vector<int32_t> stack;
    for (int32_t y = 0; y < h && static_cast<int32_t>(blobs.size()) < max_candidates; ++y) {
        for (int32_t x = 0; x < w && static_cast<int32_t>(blobs.size()) < max_candidates; ++x) {
            const size_t idx = static_cast<size_t>(y) * w + x;
            if (bm.px[idx] == 0 || seen[idx] != 0) {
                continue;
            }
            // Flood-fill this blob, collecting only its border pixels. A
            // foreground pixel is a border pixel when any 8-neighbour is
            // background or outside the image.
            std::vector<naina_point> border;
            stack.clear();
            stack.push_back(static_cast<int32_t>(idx));
            seen[idx] = 1;
            while (!stack.empty()) {
                const int32_t cur = stack.back();
                stack.pop_back();
                const int32_t cy = cur / w;
                const int32_t cx = cur % w;
                bool is_border = false;
                for (int32_t k = 0; k < 8; ++k) {
                    if (!fg(cx + dx8[k], cy + dy8[k])) {
                        is_border = true;
                        break;
                    }
                }
                if (is_border) {
                    border.push_back(naina_point{static_cast<float>(cx), static_cast<float>(cy)});
                }
                for (int32_t k = 0; k < 8; ++k) {
                    const int32_t nx = cx + dx8[k];
                    const int32_t ny = cy + dy8[k];
                    if (!fg(nx, ny)) {
                        continue;
                    }
                    const size_t nidx = static_cast<size_t>(ny) * w + nx;
                    if (seen[nidx] == 0) {
                        seen[nidx] = 1;
                        stack.push_back(static_cast<int32_t>(nidx));
                    }
                }
            }
            if (!border.empty()) {
                blobs.push_back(std::move(border));
            }
        }
    }
    return blobs;
}

}  // namespace naina::internal::db_postprocess
```

Add `src/modules/db_postprocess.cc` to `NAINA_SOURCES` in
`core/CMakeLists.txt`, after `src/geometry.cc`.

- [ ] **Step 5: Run the test to verify it passes**

```bash
cmake --build --preset macos-arm64 --target test_db_postprocess 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_db_postprocess --output-on-failure
```

Expected: `test_db_postprocess: all passed`.

- [ ] **Step 6: Commit**

```bash
git add core/src/modules/db_postprocess.hpp core/src/modules/db_postprocess.cc \
        core/tests/test_db_postprocess.cc core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "db_postprocess: binarize and 8-connected blob borders

Replaces cv2.findContours with a flood fill that keeps only border pixels
— that is all min_area_quad needs, and it avoids storing blob interiors.

8-connectivity matters: two glyph strokes touching only at a corner belong
to the same text line, and 4-connectivity would split them into two boxes.
Blob order follows a raster scan of seed pixels so output is deterministic,
which the cross-binding parity guarantee depends on."
```

---

## Task 11: Box scoring and the full DBNet decode

**Files:**
- Modify: `core/src/modules/db_postprocess.cc`
- Modify: `core/tests/test_db_postprocess.cc`

- [ ] **Step 1: Write the failing test**

Add to `core/tests/test_db_postprocess.cc` before `main`, and call from
`main`. Add these to the top: `using naina::internal::db_postprocess::box_score;`,
`using naina::internal::db_postprocess::decode;`,
`using naina::internal::db_postprocess::Config;`, and `#include <cstdint>`.

```cpp
// A probability map with a single high-confidence rectangle of text.
static std::vector<float> prob_with_rect(
    int32_t w, int32_t h, int32_t x0, int32_t y0, int32_t x1, int32_t y1, float v) {
    std::vector<float> p(static_cast<size_t>(w) * h, 0.02F);
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            p[static_cast<size_t>(y) * w + x] = v;
        }
    }
    return p;
}

static void test_box_score_averages_probability_inside() {
    const int32_t w = 20;
    const int32_t h = 20;
    const auto p = prob_with_rect(w, h, 5, 5, 14, 10, 0.9F);
    naina_point quad[4] = {{5, 5}, {14, 5}, {14, 10}, {5, 10}};
    const float s = box_score(p.data(), w, h, quad);
    EXPECT(s > 0.85F && s <= 1.0F);

    // A quad over background must score near zero.
    naina_point empty[4] = {{16, 16}, {19, 16}, {19, 19}, {16, 19}};
    EXPECT(box_score(p.data(), w, h, empty) < 0.1F);
}

static void test_decode_finds_one_box_and_unclips_it() {
    const int32_t w = 60;
    const int32_t h = 40;
    // Text block from (10,10) to (39,19): 30 wide, 10 tall.
    const auto p = prob_with_rect(w, h, 10, 10, 39, 19, 0.95F);

    Config cfg;
    cfg.thresh = 0.2F;
    cfg.box_thresh = 0.4F;
    cfg.unclip_ratio = 1.4F;
    cfg.min_box_side = 3.0F;

    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.size() == 1);
    if (boxes.empty()) {
        return;
    }
    EXPECT(boxes[0].score > 0.8F);

    // Unclip must have grown the box beyond the raw 30x10 extent.
    float minx = 1e9F, maxx = -1e9F, miny = 1e9F, maxy = -1e9F;
    for (const auto& c : boxes[0].corners) {
        minx = c.x < minx ? c.x : minx;
        maxx = c.x > maxx ? c.x : maxx;
        miny = c.y < miny ? c.y : miny;
        maxy = c.y > maxy ? c.y : maxy;
    }
    EXPECT(minx < 10.0F);
    EXPECT(maxx > 39.0F);
    EXPECT(miny < 10.0F);
    EXPECT(maxy > 19.0F);
    // But not absurdly: distance is ~ (30*10)*1.4/80 = 5.25 px per side.
    EXPECT(minx > 0.0F);
    EXPECT(maxx < 59.0F);
}

static void test_decode_rejects_low_confidence_blobs() {
    const int32_t w = 40;
    const int32_t h = 30;
    // Above the binarisation threshold (0.2) but below box_thresh (0.4).
    const auto p = prob_with_rect(w, h, 5, 5, 24, 14, 0.3F);
    Config cfg;
    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.empty());
}

static void test_decode_rejects_slivers() {
    const int32_t w = 40;
    const int32_t h = 30;
    // A 20x1 sliver: below min_box_side of 3.
    const auto p = prob_with_rect(w, h, 5, 10, 24, 10, 0.95F);
    Config cfg;
    cfg.min_box_side = 3.0F;
    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.empty());
}

static void test_decode_finds_two_blocks() {
    const int32_t w = 80;
    const int32_t h = 40;
    auto p = prob_with_rect(w, h, 5, 5, 30, 16, 0.95F);
    const auto q = prob_with_rect(w, h, 45, 5, 70, 16, 0.95F);
    for (size_t i = 0; i < p.size(); ++i) {
        p[i] = p[i] > q[i] ? p[i] : q[i];
    }
    Config cfg;
    const auto boxes = decode(p.data(), w, h, cfg);
    EXPECT(boxes.size() == 2);
}

static void test_decode_empty_map_is_empty() {
    const int32_t w = 20;
    const int32_t h = 20;
    const std::vector<float> p(static_cast<size_t>(w) * h, 0.01F);
    Config cfg;
    EXPECT(decode(p.data(), w, h, cfg).empty());
    EXPECT(decode(nullptr, w, h, cfg).empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset macos-arm64 --target test_db_postprocess 2>&1 | tail -10
```

Expected: link errors — `box_score` and `decode` are undefined.

- [ ] **Step 3: Implement `box_score`**

Add to `core/src/modules/db_postprocess.cc` inside the namespace:

```cpp
namespace {

// Is `p` inside the convex quad? Consistent sign of the cross product
// against all four edges means inside.
bool point_in_quad(const naina_point quad[4], float px, float py) {
    int32_t pos = 0;
    int32_t neg = 0;
    for (int32_t i = 0; i < 4; ++i) {
        const naina_point& a = quad[i];
        const naina_point& b = quad[(i + 1) % 4];
        const float d = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
        if (d > 0.0F) {
            ++pos;
        } else if (d < 0.0F) {
            ++neg;
        }
    }
    return pos == 0 || neg == 0;
}

}  // namespace

float box_score(const float* prob, int32_t width, int32_t height, const naina_point quad[4]) {
    if (prob == nullptr || width <= 0 || height <= 0 || quad == nullptr) {
        return 0.0F;
    }
    // Walk the quad's axis-aligned extent, averaging only pixels inside it.
    // This is PaddleOCR's box_score_fast.
    float minx = quad[0].x;
    float maxx = quad[0].x;
    float miny = quad[0].y;
    float maxy = quad[0].y;
    for (int32_t i = 1; i < 4; ++i) {
        minx = std::min(minx, quad[i].x);
        maxx = std::max(maxx, quad[i].x);
        miny = std::min(miny, quad[i].y);
        maxy = std::max(maxy, quad[i].y);
    }
    int32_t x0 = static_cast<int32_t>(std::floor(minx));
    int32_t x1 = static_cast<int32_t>(std::ceil(maxx));
    int32_t y0 = static_cast<int32_t>(std::floor(miny));
    int32_t y1 = static_cast<int32_t>(std::ceil(maxy));
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(width - 1, x1);
    y1 = std::min(height - 1, y1);
    if (x1 < x0 || y1 < y0) {
        return 0.0F;
    }

    double acc = 0.0;
    int64_t n = 0;
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            if (!point_in_quad(quad, static_cast<float>(x), static_cast<float>(y))) {
                continue;
            }
            acc += static_cast<double>(prob[static_cast<size_t>(y) * width + x]);
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(acc / static_cast<double>(n)) : 0.0F;
}
```

- [ ] **Step 4: Implement `decode`**

Append to the same namespace:

```cpp
std::vector<naina_textbox> decode(const float* prob,
                                  int32_t width,
                                  int32_t height,
                                  const Config& cfg) {
    std::vector<naina_textbox> out;
    if (prob == nullptr || width <= 0 || height <= 0) {
        return out;
    }
    const Bitmap bm = binarize(prob, width, height, cfg.thresh);
    const auto blobs = find_blob_borders(bm, cfg.max_candidates);
    out.reserve(blobs.size());

    for (const auto& border : blobs) {
        if (border.size() < 3) {
            continue;  // a single pixel or a pair cannot form a box
        }
        naina_point quad[4];
        if (!min_area_quad(border, quad)) {
            continue;
        }

        // Reject slivers before the expensive scoring pass.
        const float side_a = std::hypot(quad[1].x - quad[0].x, quad[1].y - quad[0].y);
        const float side_b = std::hypot(quad[3].x - quad[0].x, quad[3].y - quad[0].y);
        if (std::min(side_a, side_b) < cfg.min_box_side) {
            continue;
        }

        // Score on the RAW box, before unclip — unclip deliberately spills
        // into background, so scoring after it would depress every score.
        const float score = box_score(prob, width, height, quad);
        if (score < cfg.box_thresh) {
            continue;
        }

        // Unclip: DBNet trained on shrunk polygons.
        std::vector<naina_point> poly(quad, quad + 4);
        const float area = polygon_area(poly);
        const float per = polygon_perimeter(poly);
        if (per <= 0.0F) {
            continue;
        }
        const float distance = area * cfg.unclip_ratio / per;
        const auto grown = offset_convex_polygon(poly, distance);
        if (grown.size() != 4) {
            continue;
        }

        naina_textbox tb{};
        for (int32_t i = 0; i < 4; ++i) {
            // Clamp to the map so downstream warping never samples outside.
            tb.corners[i].x = std::clamp(grown[static_cast<size_t>(i)].x, 0.0F,
                                         static_cast<float>(width - 1));
            tb.corners[i].y = std::clamp(grown[static_cast<size_t>(i)].y, 0.0F,
                                         static_cast<float>(height - 1));
        }
        tb.score = score;
        out.push_back(tb);
    }
    return out;
}
```

Add `using naina::internal::min_area_quad;`, `polygon_area`,
`polygon_perimeter` and `offset_convex_polygon` into scope — the simplest way
is to reference them fully qualified as `naina::internal::min_area_quad(...)`
etc., since `db_postprocess` is a nested namespace inside `naina::internal`
and unqualified lookup already finds them. No `using` declarations needed.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset macos-arm64 --target test_db_postprocess 2>&1 | tail -5 \
  && ctest --preset macos-arm64 -R test_db_postprocess --output-on-failure
```

Expected: `test_db_postprocess: all passed`.

- [ ] **Step 6: Run the whole suite — nothing else may regress**

```bash
ctest --preset macos-arm64 --output-on-failure
```

Expected: every test passes.

- [ ] **Step 7: Commit**

```bash
git add core/src/modules/db_postprocess.cc core/tests/test_db_postprocess.cc
git commit -m "db_postprocess: box scoring and full DBNet decode

decode() is now complete: binarize -> blob borders -> min-area quad ->
sliver reject -> score -> unclip -> clamp.

Two ordering details that are easy to get wrong and are covered by tests:
scoring happens on the RAW quad before unclip, because unclip deliberately
spills into background and scoring afterwards depresses every box below
box_thresh; and slivers are rejected before scoring, which is the
expensive per-pixel pass.

Corners are clamped to the probability map so the recognition warp can
never sample outside the image."
```
