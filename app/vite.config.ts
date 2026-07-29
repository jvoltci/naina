import { defineConfig } from 'vite';
import { copyFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

// naina.wasm has to sit next to the emitted naina.mjs at runtime, and Vite will
// not trace a .wasm referenced from inside a dependency's generated glue. Copy
// it explicitly instead of relying on bundler heuristics.
const WASM_SRC = resolve(__dirname, '../bindings/wasm/dist/naina.wasm');

export default defineConfig({
  // Served from https://jvoltci.github.io/naina/ in production, / in dev.
  base: process.env.VITE_BASE ?? '/',

  plugins: [
    {
      name: 'copy-naina-wasm',
      // writeBundle rather than closeBundle: the file must exist before the
      // Pages artifact is uploaded, and closeBundle can race in some setups.
      writeBundle(options) {
        const outDir = options.dir ?? resolve(__dirname, 'dist');
        mkdirSync(outDir, { recursive: true });
        copyFileSync(WASM_SRC, resolve(outDir, 'naina.wasm'));
      },
    },
  ],

  resolve: {
    alias: {
      // @jvoltci/naina-wasm is a `file:` dependency, so npm symlinks it and Node
      // resolves that package's own imports starting from bindings/wasm/ — which
      // walks up to the repo root and never reaches app/node_modules. So
      // `import 'onnxruntime-web'` inside runtime.mjs cannot resolve unless
      // bindings/wasm has its own node_modules. It does locally (left there by
      // the Node test) and does not in CI, which is exactly how this passed here
      // and failed there.
      //
      // Pointing at the app's copy also guarantees a SINGLE ort instance. Two
      // would mean two WebAssembly runtimes in one bundle.
      'onnxruntime-web': resolve(__dirname, 'node_modules/onnxruntime-web'),
    },
    dedupe: ['onnxruntime-web'],
  },

  optimizeDeps: {
    // The Emscripten glue is already an ES module and pre-bundling it breaks
    // its import.meta.url resolution of naina.wasm.
    exclude: ['@jvoltci/naina-wasm'],
  },

  build: {
    target: 'es2022',
    sourcemap: true,
    // ort-web ships large chunks; the default 500 kB warning is pure noise here.
    chunkSizeWarningLimit: 4096,
  },

  worker: { format: 'es' },
});
