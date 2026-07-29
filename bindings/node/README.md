# @jvoltci/naina (Node binding)

Node / TypeScript binding for naina, an embeddable document-reading (OCR)
runtime. C++ core, N-API addon, zero-copy image input.

## Install

```bash
npm install @jvoltci/naina
```

## Quickstart

```ts
import { Engine, read } from '@jvoltci/naina';

const img = { data: rgbBuffer, width: 640, height: 200 }; // raw RGB8 pixels

// One-liner: image -> markdown
const markdown = await read(img);

// Or keep an Engine around for repeated calls
const engine = new Engine({ tier: 'tiny' });
const page = await engine.read(img);
console.log(page.markdown);
for (const line of page.lines) {
    console.log(line.confidence, line.text);
}

const quads = await engine.detectText(img); // geometry only, no recognition
```

`ImageInput.data` must be a raw pixel buffer (`Buffer` or `Uint8Array`) —
this package intentionally ships no image decoder. Use `sharp`, `canvas`, or
anything else that produces raw pixels.

Tiers select model size, not licence — every model naina ships is
Apache-2.0: `tiny` (~11 MB), `small` (~54 MB), `medium` (~269 MB).

Weights are fetched on first use and cached under `$NAINA_CACHE` (default
`~/.cache/naina/models`). Set `NAINA_OFFLINE=1` to disable network access
and use only the local cache.

## Build from source

```bash
npm install
npm run build   # cmake-js compile (native addon) + tsc (TypeScript)
```

The native addon links naina-core with the ONNX Runtime backend enabled
(see `CMakeLists.txt`); this requires `onnxruntime` and `libcurl` to be
discoverable by CMake.

## Test

```bash
npx vitest run
```

The smoke test suite constructs an `Engine`, checks the exported surface
and type shapes, and asserts that `read()` rejects (rather than returning
an empty page) when no model weights are available. It does not require
model weights to be present, and gates any test against real downloaded
weights behind `NAINA_E2E=1` (mirroring the Python binding's tests) — set
that, plus a scalable TrueType font on the host, to also exercise a real
read against rendered text.

Note: under vitest's default `threads` pool, `process.env` writes made
from a test file do not propagate to the native addon's `getenv()` calls
(a Node.js worker-thread limitation), so the `Engine`-dependent tests may
skip rather than run in that mode. Run with `--pool=forks` to exercise
them for real.
