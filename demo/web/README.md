# naina-web preview

Live in-browser face recognition using the same model artifacts naina's
native runtime will load. Everything runs locally — no images leave the
browser.

**Live demo:** https://JAI.github.io/naina/

## What it does

- Detects **N faces simultaneously** in the webcam stream (YuNet).
- Embeds each face to a unit vector (SFace, 128-d).
- Lets you click a face → name it → enrol it in an in-browser gallery
  (persisted to `localStorage`).
- For each future frame, every detected face is matched against the
  gallery; matches above the similarity threshold are labelled live.
- Reports per-stage latency and frame rate.

## Why "preview"?

The native [`naina-core`](../../core/) C++ runtime is in development.
This browser app uses [`onnxruntime-web`](https://onnxruntime.ai/docs/tutorials/web/)
to run the **same ONNX models** the native lib will load
(see [`models/registry.yaml`](../../models/registry.yaml)). When the
WebAssembly build of naina-core lands (v1.4), this demo swaps its
inference layer transparently — the UI code (`src/main.ts`) and the
`Engine` façade (`src/engine.ts`) don't change.

## Run locally

```bash
cd demo/web
npm install
npm run dev
```

Open the printed URL. First load downloads ~40 MB of model files into
`public/models/` (cached after that).

## Build for production

```bash
npm run build         # produces dist/
npm run preview       # serve dist/ locally to verify
```

The GitHub Pages deploy (`.github/workflows/deploy-demo.yml`) runs this
on push to `master` and publishes `dist/` to the `gh-pages` branch.

## Model choice

| Model | Task | Size | License | Source |
|---|---|---|---|---|
| `yunet.onnx`  | Face detection  | 337 KB | MIT       | [OpenCV Zoo](https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet) |
| `sface.onnx`  | Face embedding  | 37 MB  | Apache-2  | [OpenCV Zoo](https://github.com/opencv/opencv_zoo/tree/main/models/face_recognition_sface) |

These are the **default tier** models — permissively licensed,
commercial-safe. The native lib registry will additionally offer
research-tier weights (SCRFD, TransFace) gated behind an opt-in flag;
those aren't bundled in this browser demo.

## Architecture

```
   webcam  →  HTMLVideoElement
       │
       ▼
   Engine.recognizeFrame()
       │
       ├── FaceDetector.detect()    (YuNet via onnxruntime-web)
       │       │
       │       ▼   array of Face { bbox, landmarks, quality }
       │
       └── for each face:
              FaceEmbedder.embed()  (align → SFace → L2 norm)
                  │
                  ▼  Float32Array[128]
              Gallery.match()       (cosine vs enrolled identities)
                  │
                  ▼  RecognitionMatch { name, similarity }
       overlay draw  ←─────────────────────┘
```

## Notes for production use

This is a demo, not a deployment template. Real-world face recognition
needs:

- **Liveness / anti-spoofing** (planned: naina v1.0)
- **Multi-template enrolment** (multiple captures per identity)
- **Tracker** so a face keeps its identity across frames smoothly
- **Threshold calibration** against your actual user population
- **Privacy / consent flows**, retention policies, and applicable
  biometric-data regulations in your jurisdiction
