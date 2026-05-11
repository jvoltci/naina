import { defineConfig } from 'vite';

// GH Pages serves project pages under /<repo>/. Override via VITE_BASE
// when deploying to a custom domain or org root.
const base = process.env.VITE_BASE ?? '/naina/';

export default defineConfig({
  base,
  build: {
    target: 'es2022',
    sourcemap: true,
    rollupOptions: {
      output: {
        // Keep onnxruntime-web in its own chunk; it's big.
        manualChunks: {
          ort: ['onnxruntime-web'],
        },
      },
    },
  },
  server: {
    headers: {
      // Required for multi-threaded WASM in some onnxruntime-web builds.
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
  optimizeDeps: {
    exclude: ['onnxruntime-web'],
  },
});
