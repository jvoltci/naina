# naina — Architecture

> A high-performance, embeddable computer-vision runtime for face and person
> understanding. C++ core, thin bindings, runs on edge → server, one API.

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
│  │  FaceDetect · FaceAlign · FaceEmbed · Liveness       │   │
│  │  PersonDetect · Track · ReID                         │   │
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
v1.0  — Face stack solid:  detect + align + embed + verify
v1.1  — Person stack:      detect + track
v1.2  — Re-ID:             cross-camera matching
v1.3  — Liveness:          anti-spoofing (depth, RGB, motion)
```

Same API. Modules light up over time. No big-bang release.

## The model registry pattern

Two-tier weights for every task:

```yaml
- id: face_embed.default
  arch: edgeface
  license: Apache-2.0     # ships in repo, commercial-safe
  embed_dim: 512
  benchmark: { ijbc_tar_at_1e-4: 0.945, latency_ms_pi5: 4.2 }

- id: face_embed.research
  arch: transface_vit_l
  license: non-commercial # opt-in download
  embed_dim: 512
  benchmark: { ijbc_tar_at_1e-4: 0.973, latency_ms_pi5: 38.1 }
```

Users pick at init time via `config.enable_research_models`. README benchmark
table shows both columns side-by-side. We claim "SOTA accuracy" honestly
*and* the default path is permissive.

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
