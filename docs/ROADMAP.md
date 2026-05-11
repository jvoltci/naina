# Roadmap

Versioned, contributor-facing. The library ships **vertically** —
each release is a fully usable surface, not a half-built layer.

## v0.1 — Architecture spike  *(in progress)*

Locked: C ABI, C++ wrapper, backend interface, tensor types, model
registry schema, browser-side preview demo. No native inference yet.

- [x] `core/include/naina/naina.h`
- [x] `core/include/naina/naina.hpp`
- [x] `core/include/naina/backend.hpp`
- [x] `core/include/naina/tensor.hpp`
- [x] `models/registry.yaml` + JSON schema
- [x] `docs/ARCHITECTURE.md`
- [ ] Web demo + GH Pages deploy

## v0.2 — Build system & toolchain

CMake + presets, CI matrix, dep pinning. No real ML yet; just
"`libnaina.so` compiles on five targets."

- [ ] CMake root + presets (`macos-arm64`, `linux-x86_64`, `linux-arm64`,
      `android-arm64`, `ios-arm64`)
- [ ] vcpkg / Conan dep manifest
- [ ] GitHub Actions: build + smoke test matrix
- [ ] clang-format / clang-tidy gates
- [x] License switch MIT → Apache-2.0

## v0.3 — Backend layer

One model loads, runs, returns a tensor. Through two backends.

- [ ] `IBackend` / `ISession` impls for ONNX Runtime
- [ ] `IBackend` / `ISession` impls for NCNN
- [ ] `backend_registry` with runtime probe + fallback chain
- [ ] `model_loader` (manifest parse, download, sha256 verify, cache)
- [ ] `tools/onnx2ncnn.py` conversion wrapper
- [ ] Identity-model round-trip unit test

## v1.0 — Face stack solid

Face detect + align + embed + verify + liveness, both backends, Python
+ Node prebuilt binaries.

- [ ] `face_detect` (YuNet default, SCRFD research)
- [ ] `face_align` (5-pt similarity transform, SIMD)
- [ ] `face_embed` (EdgeFace default, TransFace research)
- [x] `face_liveness` module implemented (MiniFASNet-compatible; model URL pending upload)
- [ ] Python wheels: `pip install naina`
- [ ] Node prebuilds: `npm install naina`
- [ ] Eval harness: WIDERFACE, IJB-C, LFW
- [ ] Latency benchmarks: Pi5, Jetson Orin Nano, M3, x86

## v1.1 — Person stack

- [ ] `person_detect` (YOLOv10-N default, YOLOv10-X research)
- [ ] `tracker` (ByteTrack / BoT-SORT)
- [ ] `examples/multistream_cpp`
- [ ] Hailo backend (edge accelerator)

## v1.2 — Person re-identification

- [ ] `person_reid` (OSNet default, CLIP-ReID research)
- [ ] Cross-camera benchmark on MOT17 / DanceTrack
- [ ] Embedding-store reference integration (Faiss, hnswlib examples)

## v1.3 — Liveness hardening + extra bindings

- [ ] Rigorous liveness eval (CASIA-SURF, OULU-NPU)
- [ ] Rust binding (`cxx`)
- [ ] Swift binding (Apple ecosystem)
- [ ] Kotlin binding (Android)

## v1.4 — WASM target

- [ ] `naina-wasm` build via Emscripten
- [ ] Drop-in replacement for the demo's `onnxruntime-web` path
- [ ] WebGPU EP where available
- [ ] Bundle size budget: < 5 MB compressed

## v2.0 — Own training pipeline

- [ ] Training scripts (face recognition: ArcFace / AdaFace loss)
- [ ] Datasets: WebFace42M, Glint360K, MS1MV3 — license-aware
- [ ] Distilled student models for edge tiers
- [ ] Reproducible model cards in `models/`

## Non-goals (forever)

- Vector store / identity database (use Faiss / Milvus / hnswlib)
- UI / dashboards / web app builder
- Crime prediction / risk scoring (out of scope, ethically and legally)
- Government-ID matching (integrator concern, requires legal review)
- Authentication / authorization (above the library)
