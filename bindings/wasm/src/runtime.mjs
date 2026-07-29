// The JS half of naina's browser backend.
//
// Two responsibilities, both deliberately mechanical:
//
//   1. Session management — create/run/release onnxruntime-web sessions on
//      behalf of the C++ WasmJsBackend. No decisions are made here; the C++
//      core says which graph to run on which tensors.
//   2. Model staging — fetch each weight file, verify nothing (the C++ core
//      does sha256 itself), and write the bytes into Emscripten's virtual
//      filesystem at the path the core's cache layout expects.
//
// Anything that looks like OCR logic belongs in the C++ core, not here. If a
// future change needs image resizing, box decoding or text assembly on this
// side, that is a signal the split has gone wrong.

import * as ort from 'onnxruntime-web';

// naina's DType enum, mirrored. Must stay in step with core/include/naina/tensor.hpp.
const DTYPE = {
  0: 'float32',
  1: 'float16',
  2: 'bfloat16',
  3: 'int64',
  4: 'int32',
  5: 'int16',
  6: 'int8',
  7: 'uint8',
  8: 'bool',
};

// Byte width per naina DType, indexed the same way.
const DTYPE_BYTES = { 0: 4, 1: 2, 2: 2, 3: 8, 4: 4, 5: 2, 6: 1, 7: 1, 8: 1 };

// ONNX Runtime's TypedArray per element type.
const TYPED = {
  float32: Float32Array,
  float16: Uint16Array,
  bfloat16: Uint16Array,
  int64: BigInt64Array,
  int32: Int32Array,
  int16: Int16Array,
  int8: Int8Array,
  uint8: Uint8Array,
  bool: Uint8Array,
};

// naina DType -> its index, for describeIo.
const DTYPE_INDEX = Object.fromEntries(
  Object.entries(DTYPE).map(([k, v]) => [v, Number(k)]),
);

/**
 * Installs the bridge the C++ backend looks for on globalThis.
 *
 * @param {object} Module the instantiated Emscripten module
 * @param {object} [opts]
 * @param {string[]} [opts.executionProviders] ort EP preference order
 */
export function installBridge(Module, opts = {}) {
  const sessions = new Map();
  let nextHandle = 1;

  // WebGPU first where the browser has it: on a 269 MB tier it is several times
  // faster than wasm. ort silently falls back, so listing both is safe.
  const executionProviders = opts.executionProviders ?? ['webgpu', 'wasm'];

  const bridge = {
    async createSession(path, _device) {
      // The model was staged into MEMFS by stageModels; read it back out rather
      // than re-fetching, so a cached page never touches the network.
      let bytes;
      try {
        bytes = Module.FS.readFile(path);
      } catch {
        console.error(`naina: model not staged in virtual FS: ${path}`);
        return 0;
      }
      const session = await ort.InferenceSession.create(bytes, {
        executionProviders,
        graphOptimizationLevel: 'all',
      });
      const handle = nextHandle++;
      sessions.set(handle, session);
      return handle;
    },

    releaseSession(handle) {
      const s = sessions.get(handle);
      if (s) {
        // ort's release is async; nothing downstream waits on it, and dropping
        // the map entry is what makes the handle invalid.
        s.release?.();
        sessions.delete(handle);
      }
    },

    describeIo(handle, wantOutputs) {
      const s = sessions.get(handle);
      if (!s) return '[]';
      const names = wantOutputs ? s.outputNames : s.inputNames;
      const meta = wantOutputs ? s.outputMetadata : s.inputMetadata;

      const out = names.map((name, i) => {
        // outputMetadata/inputMetadata is available in ort-web 1.20+; older
        // builds expose only names. Fall back to a fully dynamic description,
        // which the C++ side tolerates because it feeds inputs BY NAME.
        const m = meta?.[i];
        const dims = (m?.dimensions ?? m?.shape ?? []).map((d) =>
          typeof d === 'number' && Number.isFinite(d) ? d : -1,
        );
        const type = m?.type ?? 'float32';
        return {
          name,
          dtype: DTYPE_INDEX[type] ?? 0,
          shape: dims,
        };
      });
      return JSON.stringify(out);
    },

    async run(
      handle,
      nIn,
      inPtrs,
      inBytes,
      inDtypes,
      inRanks,
      inShapes,
      nOut,
      outPtrs,
      outBytes,
      outDtypes,
    ) {
      const s = sessions.get(handle);
      if (!s) return 1;

      const i32 = (ptr, i) => Module.HEAP32[(ptr >> 2) + i];
      // int64 shapes arrive as a flat i64 array; read the low word, since no
      // naina tensor dimension exceeds 2^31.
      const i64lo = (ptr, i) => Module.HEAP32[(ptr >> 2) + i * 2];

      const feeds = {};
      let shapeCursor = 0;
      for (let i = 0; i < nIn; i++) {
        const dataPtr = i32(inPtrs, i);
        const byteLen = i32(inBytes, i);
        const dtypeIdx = i32(inDtypes, i);
        const rank = i32(inRanks, i);

        const dims = [];
        for (let d = 0; d < rank; d++) {
          dims.push(i64lo(inShapes, shapeCursor + d));
        }
        shapeCursor += rank;

        const typeName = DTYPE[dtypeIdx] ?? 'float32';
        const Ctor = TYPED[typeName];
        const elems = byteLen / DTYPE_BYTES[dtypeIdx];

        // Copy out of the WASM heap. A view would be cheaper, but ort may hold
        // the buffer past this call and the heap can move under memory growth.
        const view = new Ctor(Module.HEAPU8.buffer, dataPtr, elems);
        feeds[s.inputNames[i]] = new ort.Tensor(typeName, view.slice(), dims);
      }

      const results = await s.run(feeds);

      for (let i = 0; i < nOut; i++) {
        const name = s.outputNames[i];
        const t = results[name];
        if (!t) return 1;

        const dstPtr = i32(outPtrs, i);
        const dstBytes = i32(outBytes, i);
        const dtypeIdx = i32(outDtypes, i);
        const typeName = DTYPE[dtypeIdx] ?? 'float32';

        // ort may return a different element type than the caller allocated
        // for — most commonly int64 counts where naina asked for int32.
        // Converting is correct; reinterpreting the bytes would not be.
        let src = t.data;
        if (typeName === 'int32' && src instanceof BigInt64Array) {
          const conv = new Int32Array(src.length);
          for (let k = 0; k < src.length; k++) conv[k] = Number(src[k]);
          src = conv;
        } else if (typeName === 'int64' && src instanceof Int32Array) {
          const conv = new BigInt64Array(src.length);
          for (let k = 0; k < src.length; k++) conv[k] = BigInt(src[k]);
          src = conv;
        }

        const srcBytes = new Uint8Array(src.buffer, src.byteOffset, src.byteLength);
        // A model producing more than the caller allocated is a real mismatch,
        // not something to silently truncate — the C++ side sizes its buffers
        // from the graph, so this means the two disagree.
        if (srcBytes.byteLength > dstBytes) {
          console.error(
            `naina: output '${name}' is ${srcBytes.byteLength} bytes, ` +
              `buffer is ${dstBytes}`,
          );
          return 1;
        }
        Module.HEAPU8.set(srcBytes, dstPtr);
      }
      return 0;
    },
  };

  globalThis.__naina_ort = bridge;
  return bridge;
}

/**
 * Fetches the weight files for a tier and writes them into the virtual
 * filesystem where the C++ model loader expects them.
 *
 * The file list comes from Module.stagingPlan(tier) — the C++ core computes
 * both the URLs and the cache paths, so the cache layout is defined in exactly
 * one place. Do not derive paths here.
 *
 * Offline works through the Cache API: entries are keyed by URL, and every URL
 * in the registry points at an immutable, sha256-pinned GitHub release asset.
 * So a hit is always valid and no revalidation is needed. The C++ core still
 * hashes each file, which is what makes a corrupted cache entry an error rather
 * than silent garbage.
 *
 * @param {object} Module instantiated Emscripten module
 * @param {number} tier naina tier enum (1=tiny, 2=small, 3=medium)
 * @param {(done: number, total: number, path: string) => void} [onProgress]
 */
export async function stageTier(Module, tier, onProgress) {
  const files = JSON.parse(Module.stagingPlan(tier));
  if (files.length === 0) {
    throw new Error(`naina: no models in registry for tier ${tier}`);
  }
  return stageModels(Module, files, onProgress);
}

/**
 * Lower-level staging, when you already have an explicit file list.
 *
 * @param {object} Module instantiated Emscripten module
 * @param {Array<{path: string, url: string}>} files
 * @param {(done: number, total: number, path: string) => void} [onProgress]
 */
export async function stageModels(Module, files, onProgress) {
  const cache = 'caches' in globalThis ? await caches.open('naina-models-v1') : null;

  let done = 0;
  for (const { path, url } of files) {
    // Already staged in this session's FS: nothing to do.
    try {
      Module.FS.stat(path);
      onProgress?.(++done, files.length, path);
      continue;
    } catch {
      // not present; fall through and fetch
    }

    let response = cache ? await cache.match(url) : undefined;
    if (!response) {
      response = await fetch(url, { redirect: 'follow' });
      if (!response.ok) {
        throw new Error(`naina: fetch failed for ${url}: ${response.status}`);
      }
      if (cache) await cache.put(url, response.clone());
    }

    const bytes = new Uint8Array(await response.arrayBuffer());
    mkdirp(Module, path.slice(0, path.lastIndexOf('/')));
    Module.FS.writeFile(path, bytes);
    onProgress?.(++done, files.length, path);
  }
}

function mkdirp(Module, dir) {
  const parts = dir.split('/').filter(Boolean);
  let cur = '';
  for (const p of parts) {
    cur += `/${p}`;
    try {
      Module.FS.mkdir(cur);
    } catch {
      // already exists
    }
  }
}
