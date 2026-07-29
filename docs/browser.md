# Browser

The same C++ core, compiled to WebAssembly. **143 KB brotli.**

Use the hosted tool at **[jvoltci.github.io/naina](https://jvoltci.github.io/naina/)**,
or embed it.

## Install

```bash
npm install @jvoltci/naina-wasm onnxruntime-web
```

## Use

```js
import { createReader } from '@jvoltci/naina-wasm';

const reader = await createReader({
  tier: 'tiny',
  modelBaseUrl: '/models',                       // see below — required
  onProgress: (done, total) => console.log(`${done}/${total}`),
});

const markdown = await reader.readMarkdown(rgb, width, height);
const page = await reader.readJson(rgb, width, height);
```

!!! warning "You must host the model weights yourself"
    `modelBaseUrl` is not optional in practice.

    naina's registry points at GitHub release assets, which is correct for
    Python, Node and native builds — they make ordinary server-side requests. **A
    browser cannot fetch them.** A release download 302s to
    `release-assets.githubusercontent.com`, and neither hop sends an
    `Access-Control-Allow-Origin` header, so `fetch` is blocked by CORS.

    Copy the files for your tier to somewhere you serve (same-origin is
    simplest) and point `modelBaseUrl` there. naina's core still sha256-verifies
    every file, so hosting them yourself does not weaken integrity.

    `app/scripts/stage-models.mjs` in the repo does this, reading the file list
    from the core so it cannot drift.

## Getting RGB bytes

naina ships no image decoder — the browser has one.

```js
const bitmap = await createImageBitmap(fileOrBlob);
const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
const ctx = canvas.getContext('2d');
ctx.drawImage(bitmap, 0, 0);
const { data } = ctx.getImageData(0, 0, bitmap.width, bitmap.height);

const rgb = new Uint8Array(bitmap.width * bitmap.height * 3);
for (let i = 0, j = 0; i < data.length; i += 4, j += 3) {
  rgb[j] = data[i];
  rgb[j + 1] = data[i + 1];
  rgb[j + 2] = data[i + 2];
}
```

Decode at native resolution and let naina resize. If you scale with `drawImage`
first, the result depends on the browser's scaling filter — the HTML spec leaves
it implementation-defined, and Chrome, Safari and Firefox differ. naina's own
resize is the same code on every platform.

## Run it in a Worker

A full page takes one to two seconds. On the main thread that freezes the tab.
naina's module is built for `web,worker,node`, so it loads in a worker unchanged.

```js
// ocr.worker.js
import { createReader } from '@jvoltci/naina-wasm';

let reader;
self.onmessage = async ({ data }) => {
  reader ??= await createReader({ tier: 'tiny', modelBaseUrl: '/models' });
  const markdown = await reader.readMarkdown(data.rgb, data.width, data.height);
  self.postMessage({ markdown });
};
```

## Offline

Model URLs are immutable and content-pinned, so a Cache API hit never needs
revalidating. naina's runtime caches them under `naina-models-v1`.

Keep that cache **separate from your app-shell cache** and never version it with
your build. Otherwise a CSS change evicts the weights and triggers a fresh
multi-megabyte download.

## Why the reads are async

`ISession::run` is synchronous C++, but `onnxruntime-web`'s `run()` returns a
Promise. Emscripten's ASYNCIFY suspends and resumes the WASM stack across that
await, so the core stays unaware it is in a browser — which is why
`readMarkdown` returns a Promise even though nothing inside naina is async.

The alternative, `SharedArrayBuffer` + `Atomics.wait` in a worker, needs COOP and
COEP response headers. GitHub Pages cannot set headers, so it was not an option.

## Execution providers

Defaults to `['wasm']`. WebGPU is opt-in because it is currently broken in a way
that is worse than a crash — it silently drops layout detection. See
[what it cannot do](limits.md#webgpu-is-off-by-default-because-it-silently-breaks-layout).

```js
// Opt in only if you have measured it on your target browsers.
await createReader({ executionProviders: ['webgpu', 'wasm'], /* ... */ });
```

## Accuracy vs native

Close, not bit-identical, and that boundary is
[documented with numbers](limits.md#the-browser-is-close-to-native-not-bit-identical).
