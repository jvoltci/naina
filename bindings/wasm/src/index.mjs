// naina for the browser.
//
//   import { createReader } from '@jvoltci/naina-wasm';
//
//   const reader = await createReader({ tier: 'tiny', onProgress: console.log });
//   const markdown = reader.readMarkdown(rgbBytes, width, height);
//
// The heavy lifting is in the WASM core; this module just sequences module
// instantiation, bridge installation and model staging in the right order.

import createNaina from '../dist/naina.mjs';
import { installBridge, stageTier, stageModels } from './runtime.mjs';

/** naina_tier, mirrored from core/include/naina/naina.h. */
export const TIER = { auto: 0, tiny: 1, small: 2, medium: 3 };

/**
 * Instantiate naina and stage a tier's weights.
 *
 * @param {object} [opts]
 * @param {'auto'|'tiny'|'small'|'medium'} [opts.tier='tiny'] tiny is the right
 *   default for a browser: 11 MB total against small's 54 MB.
 * @param {string[]} [opts.executionProviders] ort EP order, default webgpu→wasm
 * @param {(done:number,total:number,path:string)=>void} [opts.onProgress]
 * @param {boolean} [opts.stage=true] set false to stage yourself later
 */
export async function createReader(opts = {}) {
  const tierName = opts.tier ?? 'tiny';
  const tier = TIER[tierName];
  if (tier === undefined) {
    throw new Error(`naina: unknown tier '${tierName}'`);
  }

  const Module = await createNaina();
  installBridge(Module, { executionProviders: opts.executionProviders });

  if (opts.stage !== false) {
    await stageTier(Module, tier, opts.onProgress);
  }

  // NAINA_BACKEND_AUTO: the registry picks onnxruntime-web because it is the
  // only backend compiled into this build.
  const reader = new Module.Reader(tier, 0);
  if (!reader.ok()) {
    const msg = Module.statusText(reader.status());
    reader.delete();
    throw new Error(`naina: init failed: ${msg}`);
  }

  // Both read methods are async, and that is a consequence of ASYNCIFY rather
  // than a design choice: ISession::run is synchronous C++, but it suspends on
  // ort-web's Promise, so Emscripten returns a Promise to the JS caller. The
  // core stays sync; only this boundary is async.
  return {
    /** @returns {Promise<string>} markdown, or '' on failure (see lastError). */
    readMarkdown: (rgb, w, h) => reader.readMarkdown(rgb, w, h),
    /** @returns {Promise<object|null>} lines, regions and confidences. */
    async readJson(rgb, w, h) {
      const s = await reader.readJson(rgb, w, h);
      return s ? JSON.parse(s) : null;
    },
    lastError: () => Module.statusText(reader.lastError()),
    version: () => Module.version(),
    /** Release the native context. The reader is unusable afterwards. */
    close: () => reader.delete(),
    /** Escape hatch for callers that need the raw module (FS, heap, ...). */
    module: Module,
  };
}

export { installBridge, stageTier, stageModels };
