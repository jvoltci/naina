import { defineConfig } from 'vite';
import { copyFileSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

// naina.wasm has to sit next to the emitted naina.mjs at runtime, and Vite will
// not trace a .wasm referenced from inside a dependency's generated glue. Copy
// it explicitly instead of relying on bundler heuristics.
const WASM_SRC = resolve(__dirname, '../../bindings/wasm/dist/naina.wasm');

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
