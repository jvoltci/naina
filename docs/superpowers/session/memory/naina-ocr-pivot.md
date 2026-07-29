---
name: naina-ocr-pivot
description: "naina (~/Documents/code/naina) is being rebuilt from a face-recognition runtime into an embeddable OCR/document-reading runtime; thesis is distribution, not accuracy"
metadata: 
  node_type: memory
  type: project
  originSessionId: 484389e7-4ff9-49a3-bfd5-d8f73e5fe0e7
  modified: 2026-07-29T05:57:20.127Z
---

`naina` lives at `/Users/shivya/Documents/code/naina`, not in the working
directory. Work is directed there from `~/Documents/volt/v9`, which holds only a
CLAUDE.md. As of 2026-07-29 all work is on branch `ocr-pivot`; `master` is
untouched and the old face code is preserved on branch `face-stack`.

**The thesis, which shapes every decision:** OCR accuracy is a commodity —
PP-OCRv6's weights are Apache-2.0, so naina runs the same models PaddleOCR runs
and gets the same accuracy. Competing on accuracy is unwinnable. The gap is
**distribution**: no OCR library ships one engine that runs identically on
server, laptop, phone, Pi and browser. This is llama.cpp's playbook applied to
OCR. Consequences that follow from it and should not be re-litigated:

- No OpenCV, no pyclipper, no PaddlePaddle. Convex hull, minimum-area rectangle,
  polygon offset and contour tracing are hand-written C++ because a 300 MB
  dependency tree would defeat an 11 MB tier.
- Tiers are a **device** axis (tiny/small/medium), never a licence axis.
- Model weights are mirrored to naina's own GitHub release, sha256-pinned, with
  upstream kept only as `source_url` provenance.

The name means *eyes* in Hindi, which is why OCR fits it better than face
recognition ever did.

See [[verify-by-running-not-reading]] for the working lesson this project
produced, and [[jvoltci-package-conventions]] for the house style its docs follow.
