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
  | { id: number; kind: 'warm'; tier: TierName }
  | {
      id: number;
      kind: 'read';
      tier: TierName;
      rgb: Uint8Array;
      width: number;
      height: number;
    };

export type WorkerResponse =
  | { id: number; kind: 'progress'; done: number; total: number; name: string }
  | { id: number; kind: 'stage'; label: string }
  | { id: number; kind: 'warmed'; tier: TierName; version: string }
  | { id: number; kind: 'result'; markdown: string; page: NainaPage | null; ms: number }
  | { id: number; kind: 'error'; message: string };

const post = (msg: WorkerResponse) => self.postMessage(msg);

// One reader per tier, kept alive: constructing one downloads weights, so a user
// toggling back to a tier they already used should pay nothing.
const readers = new Map<TierName, Reader>();

async function readerFor(tier: TierName, id: number): Promise<Reader> {
  const existing = readers.get(tier);
  if (existing) return existing;

  const reader = await createReader({
    tier,
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
  readers.set(tier, reader);
  return reader;
}

self.addEventListener('message', async (event: MessageEvent<WorkerRequest>) => {
  const req = event.data;
  try {
    if (req.kind === 'warm') {
      const reader = await readerFor(req.tier, req.id);
      post({ id: req.id, kind: 'warmed', tier: req.tier, version: reader.version() });
      return;
    }

    const reader = await readerFor(req.tier, req.id);
    post({ id: req.id, kind: 'stage', label: 'Reading' });

    const started = performance.now();
    const markdown = await reader.readMarkdown(req.rgb, req.width, req.height);
    const page = await reader.readJson(req.rgb, req.width, req.height);
    const ms = performance.now() - started;

    if (!markdown && !page) {
      post({ id: req.id, kind: 'error', message: reader.lastError() });
      return;
    }
    post({ id: req.id, kind: 'result', markdown, page, ms });
  } catch (e) {
    post({ id: req.id, kind: 'error', message: e instanceof Error ? e.message : String(e) });
  }
});
