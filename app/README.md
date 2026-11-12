# naina — the web app

Free OCR that runs entirely in the browser. Live at
**[jvoltci.github.io/naina](https://jvoltci.github.io/naina/)**.

Drop a PDF or image, get text and structured markdown back. No upload, no
account, no page limit. Works offline once the weights are cached.

## What runs where

| | Where |
|---|---|
| PDF rasterisation | pdf.js, main thread |
| Image decode | browser `createImageBitmap` |
| **Detection, recognition, layout, reading order, markdown** | **naina's C++ core, in a Web Worker** |
| ONNX graph execution | onnxruntime-web |

OCR is in a worker on purpose: a full page takes a second or two, and on the main
thread that freezes the tab — no scrolling, no progress, no cancel.

Nothing in `src/` decodes boxes or processes images. That work lives in the C++
core so this tool and naina's Python/Node packages give the same answers.

## Model weights are served from this site, not from GitHub releases

This surprised me, so it is written down: **GitHub release assets cannot be
fetched by a browser.** A release download 302s to
`release-assets.githubusercontent.com`, and neither hop sends an
`Access-Control-Allow-Origin` header, so `fetch` is blocked by CORS.

Every other naina binding is fine — Python, Node and native builds make ordinary
server-side requests. Only the browser is affected.

So `deploy-web.yml` downloads the tiny and small weights from the release at
build time and includes them in the Pages artifact, and the app passes
`modelBaseUrl` pointing at itself. The weights are never committed to git, and
naina's C++ core still sha256-verifies every file, so integrity is unchanged.

The medium tier is not offered here: `ppdoclayout_l.onnx` is 129 MB and GitHub
Pages caps a single file at 100 MB. It is also the wrong choice for a browser.

## Execution providers

Defaults to `['wasm']`. WebGPU is opt-in because it is measurably broken today —
Chrome 141 on an M3 initialises the JSEP provider and then fails a MatMul kernel:

```
[E:onnxruntime] Non-zero status code returned while running MatMul node.
Name:'MatMul.3' Status Message: Failed to run JSEP kernel
```

ORT recovers per-node, so text recognition still returned 33 lines at 0.99 mean
confidence and everything *looked* fine. But layout detection returned **0
regions instead of 9**, so the markdown lost all structure. A silent quality
regression is worse than a crash. Revisit when there are real numbers.

## Development

```bash
# 1. Build the WASM binding (needs Emscripten)
cd .. && emcmake cmake -S . -B build/wasm \
  -DNAINA_BUILD_WASM=ON -DNAINA_WITH_WASMJS=ON -DNAINA_BUILD_TESTS=OFF \
  -DNAINA_BUILD_SHARED=OFF -DNAINA_VENDOR_YAMLCPP=ON -DNAINA_RELEASE=ON
cmake --build build/wasm

# 2. Stage weights into public/models/ (gitignored)
#    Any prior native run leaves them in ~/.cache/naina/models.

# 3. Run
cd app && npm i && npm run dev
```

### Tests

```bash
npm run build
NAINA_E2E_IMAGE=/path/page.png NAINA_E2E_PDF=/path/doc.pdf node test/e2e.mjs
```

Drives real Chrome through Playwright. This is the only test that covers
OffscreenCanvas, `createImageBitmap`, the module worker, pdf.js and ASYNCIFY
inside a worker — and it caught both the CORS problem and the WebGPU regression
above, neither of which any off-browser test could see.
