# naina examples

Working end-to-end examples for each binding.

**Status:** the face-verification examples that lived here moved to the
`face-stack` branch along with the face modules. OCR examples land with
v0.2 (text spotting) — see
`docs/design/plans/2026-07-29-naina-ocr-core.md`.

## Model cache

Both of these are implemented in `core/src/model_loader.cc` and apply to
every binding:

- `NAINA_CACHE=/some/dir` relocates the weight cache. Default is
  `~/.cache/naina/models/`.
- `NAINA_OFFLINE=1` disables network fetches entirely. A missing weight
  then returns `NAINA_E_MODEL_NOT_FOUND` instead of downloading. Useful
  for tests and air-gapped runs.

Every download is verified against the sha256 recorded in
`models/registry.yaml`, so a truncated or substituted file fails closed
rather than producing silently wrong output.
