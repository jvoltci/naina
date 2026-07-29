// OCR runs here, never on the main thread.
//
// A full-page read at the small tier takes seconds. On the main thread that
// freezes the tab: no scrolling, no cancel, no progress paint. Since naina's
// WASM module is built with ENVIRONMENT=web,worker,node it loads here unchanged.
//
// The protocol is deliberately small — one request type per thing the UI needs.

import { createReader, type NainaPage, type TierName } from '@jvoltci/naina-wasm';

type Reader = Awaited<ReturnType<typeof createReader>>;

export type WorkerRequest =
  | { id: number; kind: 'warm'; tier: TierName; language: string }
  | {
      id: number;
      kind: 'read';
      tier: TierName;
      /** '' for Latin+CJK, a script name, or 'auto' to detect. */
      language: string;
      rgb: Uint8Array;
      width: number;
      height: number;
      /** Optional downscaled copy, used only for script probing. */
      probe?: { rgb: Uint8Array; width: number; height: number };
    };

export type WorkerResponse =
  | { id: number; kind: 'progress'; done: number; total: number; name: string }
  | { id: number; kind: 'stage'; label: string }
  | { id: number; kind: 'warmed'; tier: TierName; version: string }
  | {
      id: number;
      kind: 'result';
      markdown: string;
      page: NainaPage | null;
      ms: number;
      /** Which alphabet produced this, once auto-detection has resolved. */
      language: string;
      /** True when the alphabet was detected rather than chosen by the caller. */
      detected: boolean;
    }
  | { id: number; kind: 'error'; message: string };

const post = (msg: WorkerResponse) => self.postMessage(msg);

// One reader per tier, kept alive: constructing one downloads weights, so a user
// toggling back to a tier they already used should pay nothing.
// Keyed by tier AND language: they select different recognition models, so one
// reader cannot serve both.
const readers = new Map<string, Reader>();

async function readerFor(tier: TierName, language: string, id: number): Promise<Reader> {
  const key = `${tier}/${language}`;
  const existing = readers.get(key);
  if (existing) return existing;

  const reader = await createReader({
    tier,
    language,
    // Weights are served from this deployment, not from GitHub releases: a
    // release download redirects to release-assets.githubusercontent.com and
    // neither hop sends Access-Control-Allow-Origin, so a cross-origin fetch is
    // blocked. deploy-web.yml stages the files into the site at build time.
    modelBaseUrl: new URL(`${import.meta.env.BASE_URL}models`, self.location.href).href,
    onProgress: (done, total, path) => {
      // Strip the cache layout's sha256 prefix; users do not need to see it.
      const name = path.slice(path.lastIndexOf('/') + 1).replace(/^[0-9a-f]{16}__/, '');
      post({ id, kind: 'progress', done, total, name });
    },
  });
  readers.set(key, reader);
  return reader;
}

// Script auto-detection lives in the C++ core (`language: "auto"`), not here.
//
// It used to be implemented in this file: read with the default, and if mean
// confidence fell below a gate, re-read a downscaled copy with each other
// alphabet. That worked, but it put a decision in TypeScript that every other
// binding would then have had to reimplement -- the precise failure mode naina
// exists to avoid. The core version is also cheaper: it votes on a sample of the
// eight highest-scoring detected boxes rather than re-reading whole pages.
//
// The measured basis for the thresholds is documented at detect_language() in
// core/src/api.cc.

self.addEventListener('message', async (event: MessageEvent<WorkerRequest>) => {
  const req = event.data;
  try {
    if (req.kind === 'warm') {
      const reader = await readerFor(req.tier, req.language, req.id);
      post({ id: req.id, kind: 'warmed', tier: req.tier, version: reader.version() });
      return;
    }

    const reader = await readerFor(req.tier, req.language, req.id);
    post({ id: req.id, kind: 'stage', label: 'Reading' });

    const started = performance.now();
    const markdown = await reader.readMarkdown(req.rgb, req.width, req.height);
    const page = await reader.readJson(req.rgb, req.width, req.height);


    const ms = performance.now() - started;
    if (!markdown && !page) {
      post({ id: req.id, kind: 'error', message: reader.lastError() });
      return;
    }
    post({
      id: req.id,
      kind: 'result',
      markdown,
      page,
      ms,
      // The core resolved 'auto'; it reports the alphabet it used in the page.
      language: page?.language ?? (req.language === 'auto' ? '' : req.language),
      detected: req.language === 'auto',
    });
  } catch (e) {
    post({ id: req.id, kind: 'error', message: e instanceof Error ? e.message : String(e) });
  }
});
