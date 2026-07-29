# naina — WASM binding

naina's C++ core compiled to WebAssembly. OCR entirely in the browser: no
upload, no server, offline after the first visit.

```js
import { createReader } from '@jvoltci/naina-wasm';

const reader = await createReader({
  tier: 'tiny',                                  // 11 MB of weights
  onProgress: (done, total) => console.log(`${done}/${total}`),
});

// rgb is a Uint8Array of packed RGB8 bytes, w*h*3 long.
const markdown = await reader.readMarkdown(rgb, width, height);
const page = await reader.readJson(rgb, width, height);
```

## Getting RGB bytes from an image

naina ships no image decoder on purpose — the browser already has one.

```js
const bitmap = await createImageBitmap(fileOrBlob);
const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
const ctx = canvas.getContext('2d');
ctx.drawImage(bitmap, 0, 0);
const { data } = ctx.getImageData(0, 0, bitmap.width, bitmap.height);

// getImageData gives RGBA; naina wants RGB.
const rgb = new Uint8Array(bitmap.width * bitmap.height * 3);
for (let i = 0, j = 0; i < data.length; i += 4, j += 3) {
  rgb[j] = data[i];
  rgb[j + 1] = data[i + 1];
  rgb[j + 2] = data[i + 2];
}
```

Decode at native resolution and let naina do the resizing. If you scale the
image with `drawImage` first, the result depends on the browser's scaling filter,
which the HTML spec leaves implementation-defined — Chrome, Safari and Firefox
disagree. naina's own resize is the same code on every platform.

## Why the reads are async

`ISession::run` is synchronous C++, but `onnxruntime-web`'s `run()` returns a
Promise. Emscripten's ASYNCIFY suspends and resumes the WASM stack across that
await, which is what lets the core stay unaware that it is in a browser — so
`readMarkdown` and `readJson` return Promises even though nothing inside naina
is async.

The alternative, `SharedArrayBuffer` + `Atomics.wait` in a worker, needs COOP and
COEP response headers. GitHub Pages cannot set headers, so it was not an option.

## What runs where

Everything naina computes runs in the shared C++ core: detection resize
geometry, DBNet post-processing, quad rectification, CTC decoding, layout class
mapping, reading order and markdown assembly. JavaScript does exactly two
things — execute an ONNX graph, and fetch bytes.

That split is deliberate. Those ~1,950 lines of arithmetic have many knobs
(rounding mode, quad corner ordering, CTC blank handling, scoring before unclip
rather than after) that fail *silently* when they diverge rather than raising an
error. A second implementation in TypeScript would need to match all of them
forever.

## Size

| File | Raw | Brotli |
|---|---|---|
| `naina.wasm` | 616 KB | **143 KB** |
| `naina.mjs` | 100 KB | 24 KB |

`onnxruntime-web` is a peer dependency and is not counted here; it is the larger
download. Model weights are 11 MB (tiny) to 269 MB (medium), fetched once and
then served from the Cache API.

## Honest limits

**Browser output is close to native, but not bit-identical.** onnxruntime-web is
a different build of ONNX Runtime — WASM SIMD kernels instead of native
NEON/AVX — so probability maps differ in the last few float bits. Measured on an
A4 page at tiny tier: native macOS arm64 produced 35 text lines, WASM 33, with
33 character-identical. The difference was one marginal blob landing on the other
side of DBNet's 0.3 binarize threshold, which changed line segmentation; because
a split fragment takes its own reading-order slot, word order can shift with it.

naina's byte-identical guarantee covers *bindings over one backend build*
(Python, Node, Rust). What this binding guarantees is determinism within itself
and the same algorithms everywhere.

**No Devanagari, and other unsupported scripts read as confident nonsense.**
PP-OCRv6's charset does not cover them, and nothing in the output yet signals
that. Tracked in [the roadmap](../../docs/ROADMAP.md).

## Building

Needs Emscripten on `PATH`.

```bash
emcmake cmake -S . -B build/wasm \
  -DNAINA_BUILD_WASM=ON -DNAINA_WITH_WASMJS=ON \
  -DNAINA_BUILD_TESTS=OFF -DNAINA_BUILD_SHARED=OFF \
  -DNAINA_VENDOR_YAMLCPP=ON -DNAINA_RELEASE=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm
```

Then, with weights already in the host cache from any native run:

```bash
cd bindings/wasm && npm i
NAINA_WASM_FIXTURE=/path/to/page.rgb npm test
```

The test skips loudly, naming what is missing, rather than passing vacuously.
