# naina — Architecture

> A high-performance, embeddable document-reading runtime. C++ core, thin
> bindings, runs on edge → server, one API.
>
> naina began as a face and person understanding runtime. The engine described
> below is that work; v0.2 repurposed it for OCR, which the name always suited
> (*naina* means eyes). The face modules live on the `face-stack` branch.

## North star

- **Plug-and-play**: one C ABI, bindings everywhere (Py, Node, Rust, Swift, Kotlin, WASM).
- **Edge-first**: must run on a Raspberry Pi 5 and a Jetson Nano. Server is a bonus tier.
- **SOTA, swappable**: model weights are not hardcoded. New SOTA paper drops →
  new manifest entry, no code change.
- **Honest benchmarks**: every model in the registry ships with reproducible
  accuracy + latency numbers per target. README shows both *default
  (commercial-OK)* and *research (max accuracy)* columns.
- **Open source first-class**: Apache-2.0 code, permissive default weights,
  opt-in research weights. Adoption > bragging rights — but we get both.

## Layered design

```
┌─────────────────────────────────────────────────────────────┐
│                    Language Bindings                         │
│   Python │ Node │ Rust │ Swift │ Kotlin │ C/C++ │ WASM      │
└────────────────────────┬────────────────────────────────────┘
                         │  Stable C ABI  (naina.h)
┌────────────────────────▼────────────────────────────────────┐
│                    naina-core  (C++20)                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Pipeline  (zero-copy, async, batched DAG)           │   │
│  │  Source → Decode → Preprocess → Infer → Postprocess  │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Task modules  (independently shippable)             │   │
│  │  TextDetect · TextRectify · TextRecognize            │   │
│  │  LayoutDetect · DocAssemble                          │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Backend abstraction  (IBackend / ISession)          │   │
│  │  ┌────────┬─────────┬─────┬─────┬───────┬──────────┐ │   │
│  │  │ ONNXrt │ OpenVINO│ NCNN│ MNN │CoreML │ TensorRT │ │   │
│  │  └────────┴─────────┴─────┴─────┴───────┴──────────┘ │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  HAL  —  CPU SIMD │ GPU │ NPU │ Hailo │ Coral │ ANE │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## The four hard decisions, locked

| Decision | Choice | Reason |
|---|---|---|
| API contract | **C ABI** (`naina.h`) | Everything else (Py/Rust/Swift/WASM) is glue. llama.cpp-grade portability. |
| Model exchange | **ONNX as source of truth** | Convert to NCNN/CoreML/TRT at install or build time. Single graph, many deployments. |
| Backend selection | **Runtime, not compile-time** | Probe what's available, pick best, fall back. Build flags toggle *availability*, not *use*. |
| Model loading | **Manifest-driven** | YAML registry: URL, hash, preprocessing, postprocessing, license, benchmarks. New SOTA = new YAML entry. |

## Module modularity — why v1 isn't vaporware

Each task module is independently shippable:

```
v0.2  — Text spotting:  detect + rectify + recognise        (shipped)
v0.3  — Structure:      layout + reading order + markdown
v0.4  — Browser:        WASM target + client-side PWA
v1.0  — Guarantees:     cross-binding parity, benchmarks, MCP
```

Same API. Modules light up over time. No big-bang release.

Two of the five modules — `TextRectify` and `DocAssemble` — touch no model at
all. They are deterministic pure functions. That is deliberate: it makes the
cross-binding identity guarantee provable with plain equality assertions rather
than float-tolerance comparisons.

## The model registry pattern

Weights are tiered by **device**, not by licence. Every model naina ships is
Apache-2.0, so a permissive-vs-research split would carry no information; what
actually varies is size and the hardware it suits.

```yaml
- id: text_recognize.tiny
  task: text_recognize
  tier: tiny              # ~6 MB with det — browser, phone, Pi Zero
  arch: pp_ocrv6_rec
  license: Apache-2.0
  files:
    onnx:
      url:        "${release_base}/ppocrv6_tiny_rec.onnx"
      source_url: "${hf}/PP-OCRv6_tiny_rec_onnx/resolve/main/inference.onnx"
      sha256:     "9ef676d6ed3c88256a2d92c640c44f25b0c40947e111b14b8be8f594091563e6"
      bytes:      4462639
  output:
    type: ctc_logits
    postprocess: { blank_index: 0, num_classes: 6906 }
```

Three properties of this that matter:

**`url` points at naina's own release, not upstream.** The sha256 already meant
an upstream swap failed closed rather than corrupting output — but it would
still have broken. Mirroring removes the third-party runtime dependency
entirely. `source_url` records provenance and is never fetched.

**A request for a tier that lacks a task falls back to a larger one** rather
than failing. `layout_detect` exists only at `medium` today, so `tiny` and
`small` layout requests resolve there.

**Per-tier postprocessing is data, not code.** `num_classes` is 6906 for
`tiny` and 18710 for `small`/`medium`, because their charsets genuinely differ.
Hardcoding one value shifts every decoded character.

## Hot-path discipline (the engineering moat)

This is where naina earns the "super fast" claim — these are non-negotiable
for the C++ implementation phase:

1. **Zero allocations per frame** in steady state. Arena allocator per pipeline.
2. **No exceptions across ABI**, status codes only. Internal C++ may throw but never cross the C boundary.
3. **Zero-copy pixels** end-to-end: V4L2 / GStreamer / DMA-BUF → GPU/NPU directly. Host roundtrips are the enemy.
4. **Dynamic batching** across streams. One camera = single-frame inference; eight cameras = batched.
5. **Async pipeline**: detection on frame N runs concurrent with embedding on frame N-1.
6. **Quantization-aware**: every model has FP32 / FP16 / INT8 variants in the registry. INT8 is default on edge.
7. **SIMD everywhere** image ops live: NEON on ARM, AVX2/AVX-512 on x86, via a thin HAL.

## Deployment matrix

| Target | Primary backend | Fallback |
|---|---|---|
| Raspberry Pi 5 (ARM Cortex-A76) | NCNN (INT8) | ONNX Runtime CPU |
| Jetson Orin Nano | TensorRT (FP16) | ONNX Runtime CUDA |
| Intel NUC / industrial PC | OpenVINO | ONNX Runtime CPU |
| Apple Silicon / iPhone | CoreML (ANE) | ONNX Runtime CoreML EP |
| Android phone | NCNN Vulkan | ONNX Runtime NNAPI |
| Hailo-8 / Coral Edge TPU | vendor SDK | ONNX Runtime CPU |
| x86 server w/ NVIDIA GPU | TensorRT | ONNX Runtime CUDA |
| Browser (stretch) | ONNX Runtime Web (WASM SIMD) | — |

## Out of scope (by design, not by accident)

- **Identity database / vector store** — return embeddings, integrators choose Faiss/Milvus/hnswlib.
- **UI / dashboards** — naina is a runtime, not an app.
- **Authentication / authorization** — concerns above the library.
- **Government ID linkage / "crime prediction"** — biometric ID against
  state databases is regulated (EU AI Act high-risk, India DPDP). Library
  stays identity-agnostic; that's an integrator concern with its own
  legal review.

## Open questions to answer before v1.0

- **Training pipeline.** Wrap pretrained for v1, train own weights for v2?
- **Backend priority.** Which 2 backends ship first? My pick: ONNX Runtime (portability) + NCNN (edge perf).
- **Tracking lib.** Adapt ByteTrack ourselves, or wrap an existing C++ port?
- **CI matrix.** Which targets do we gate releases on?
