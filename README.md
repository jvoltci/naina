<div align="center">

![naina](https://github.com/jvoltci/naina/blob/master/images/naina.jpg)

# naina

**An embeddable computer-vision runtime for face & person understanding.**
*C++ core, plug-and-play bindings, runs everywhere — Pi to phone to GPU server.*

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()
[![PyPI](https://img.shields.io/badge/pip-naina-blue.svg)]()
[![npm](https://img.shields.io/badge/npm-naina-cb3837.svg)]()

[**Live demo**](https://jvoltci.github.io/naina/) · [Architecture](docs/ARCHITECTURE.md) · [Roadmap](docs/ROADMAP.md)

</div>

---

## What is naina?

A single C++ runtime that does **face detection, alignment, recognition,
liveness, person detection, tracking, and re-identification** — exposed
through one stable C ABI with first-class **Python and Node bindings**.

Built so you can:

```bash
pip install naina       # Python
npm  install naina      # Node / TypeScript
```

…and ship the same model to a Raspberry Pi, a phone, an Apple Silicon
laptop, or a CUDA server with no code change. Backends auto-select at
runtime (ONNX Runtime · NCNN · OpenVINO · CoreML · TensorRT · ExecuTorch).

## Why another CV library?

| | OpenCV | InsightFace | face-api.js | **naina** |
|---|---|---|---|---|
| Drop-in for **Python + Node + C++** | partial | py only | js only | **yes** |
| Edge-first (Pi5, Jetson, phones) | yes | partial | partial | **yes** |
| SOTA face recognition models | no | yes | dated | **yes** |
| Permissive license | Apache-2.0 | non-comm | MIT | **MIT/Apache-2.0** |
| Live in-browser demo | — | — | yes | **yes** |
| Single API across all targets | no | py only | js only | **yes** |

## 60-second quickstart

### Python

```python
import naina
import cv2

engine  = naina.Engine()                  # auto-selects best backend
img_a   = cv2.imread("alice_1.jpg")[:, :, ::-1]   # BGR -> RGB
img_b   = cv2.imread("alice_2.jpg")[:, :, ::-1]

faces_a = engine.detect_faces(img_a)
faces_b = engine.detect_faces(img_b)
emb_a   = engine.embed_face(img_a, faces_a[0])
emb_b   = engine.embed_face(img_b, faces_b[0])

print("similarity:", naina.similarity(emb_a, emb_b))   # 0..1, higher = same
```

### Node / TypeScript

```ts
import { Engine, similarity, loadImage } from 'naina';

const engine = new Engine();
const a = await loadImage('alice_1.jpg');
const b = await loadImage('alice_2.jpg');

const facesA = await engine.detectFaces(a);
const facesB = await engine.detectFaces(b);
const embA   = await engine.embedFace(a, facesA[0]);
const embB   = await engine.embedFace(b, facesB[0]);

console.log('similarity:', similarity(embA, embB));
```

### Browser (live demo)

[**jvoltci.github.io/naina**](https://jvoltci.github.io/naina/) — open it in any
modern browser and run live face recognition on your webcam, no install.
Detects **N faces simultaneously**, lets you enrol any of them, then
recognises them across frames. Same models as the native lib.

## Capabilities

**v1.0 — face stack**
- Face detection (multi-scale, multi-face)
- Face alignment (5-point similarity transform)
- Face embedding & verification (512-d L2-normalized)
- Liveness / anti-spoofing

**v1.1+ — person stack**
- Person detection
- Multi-object tracking
- Person re-identification

## Benchmarks

> Numbers are populated by `benchmarks/runner.py` and refreshed every
> release. Hardware spec in `benchmarks/results/`.

### Face recognition accuracy

| Model | License | IJB-C TAR@FAR=1e-4 | LFW |
|---|---|---|---|
| EdgeFace-XS *(default)*        | MIT          | _populating_ | _populating_ |
| TransFace-L *(research, opt-in)* | non-comm | _populating_ | _populating_ |

### Latency, end-to-end (detect + align + embed, single face, 640×480)

| Target | Backend | Default tier | Research tier |
|---|---|---|---|
| Raspberry Pi 5             | NCNN INT8        | _populating_ ms | _populating_ ms |
| Jetson Orin Nano           | TensorRT FP16    | _populating_ ms | _populating_ ms |
| Apple M3 Pro               | CoreML (ANE)     | _populating_ ms | _populating_ ms |
| x86 server (CPU only)      | ONNX Runtime     | _populating_ ms | _populating_ ms |
| x86 server (RTX 4090)      | TensorRT FP16    | _populating_ ms | _populating_ ms |

## How it works

```
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ Python (pip) │  │ Node (npm)   │  │ Web (CDN)    │
└──────┬───────┘  └──────┬───────┘  └──────┬───────┘
       │                 │                 │
       └────────  C ABI  ┴─────────────────┘
                         │              (WASM build)
                  ┌──────▼───────┐
                  │  naina-core  │   C++20, no exceptions across ABI
                  │   modules    │   face/person/track/reid/liveness
                  │   backends   │   ONNXRT · NCNN · OpenVINO · CoreML · TRT
                  │   HAL/SIMD   │   NEON · AVX2 · AVX-512
                  └──────────────┘
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full layered
design, locked decisions, and deployment matrix.

## Project status

**Pre-alpha.** Architecture spike committed; v1.0 face stack in progress.
The web demo runs the same model artifacts the native lib will load —
it's a preview, not the final implementation. See
[docs/ROADMAP.md](docs/ROADMAP.md) for what ships when.

## Contributing

Issues and PRs welcome once v0.1 lands. Until then, file design feedback
on the architecture doc.

## License

Apache-2.0. Default model weights are permissive-licensed and ship
with the library. Research-tier weights are opt-in and may carry
non-commercial restrictions; see `models/registry.yaml` per-model.
