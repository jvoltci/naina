# Session record

Context from the 2026-07-29 session that took naina from a face-recognition
runtime to a published document-reading runtime on five registries.

## `memory/`

Four short notes, committed because they are durable project context rather than
conversation:

| File | What it holds |
|---|---|
| `naina-ocr-pivot.md` | The thesis — distribution, not accuracy — and the constraints that follow (no OpenCV, tiers as a device axis, mirrored weights) |
| `naina-known-gaps.md` | What is still open, the platform floors inherited from ONNX Runtime, and the traps (clang-format vs `EM_JS`, embedded registry, raw-SVG content types, release-asset CORS, WebGPU) |
| `verify-by-running-not-reading.md` | Every confidently-wrong claim this project produced and how each was caught |
| `jvoltci-package-conventions.md` | House style for READMEs, docs sites and packaging |

## The transcript

`transcript-*.jsonl` is kept locally and **gitignored** — around 11 MB of
conversation, which is not repository material and would sit in git history
permanently. Transcripts can also carry pasted credentials: one npm token was
found in this one and redacted, and it had already been revoked.

If you need it, it is beside this file locally.

## Worth knowing before changing anything here

Every serious bug in this project failed **silently** rather than loudly:

- Quad corner ordering turned `naina 2026` into `9z07 euieu`
- WebGPU dropped layout from 9 regions to 0 while text held at 0.99
- `clang-format` rewrote `===` to `== =` inside an `EM_JS` macro
- A Hindi page returned fluent fiction at 0.758 confidence
- The JS bridge on `globalThis` let one Reader read another's WASM heap
- A green test suite that skipped every test needing a backend

None raised an error. Check the **structure** of the output, not just that
output appeared.
