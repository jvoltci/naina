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

// ── automatic script selection ────────────────────────────────────────
//
// Measured across four scripts, and the numbers decide the design:
//
//   image     best alphabet     its mean   default's mean   margin
//   Hindi     devanagari        0.937      0.511            +0.426
//   Cyrillic  cyrillic          0.998      0.894            +0.104
//   Greek     el                0.979      0.913            +0.066
//   Latin     arabic (!) 0.989  --         0.983            +0.006
//
// Two things follow.
//
// 1. Comparing alphabets on the SAME image works; an absolute threshold does
//    not. Cyrillic read with the wrong Devanagari model still scored 0.918 --
//    above any cutoff that would catch Hindi-read-as-Latin at 0.511.
//
// 2. On Latin input every alphabet ties near 0.98, because they all contain
//    Latin. Plain argmax picks `arabic` for an English page. So the default has
//    to be displaced by a MARGIN rather than merely beaten.
//
// GATE exists so the common case is free: a clean Latin page reads at ~0.98,
// never trips the gate, and costs one read. 0.95 sits above every correct
// reading measured (lowest was Hindi at 0.937, so it may probe unnecessarily
// there -- a wasted probe, not a wrong answer) and above every wrong one.
//
// MARGIN of 0.03 has 2x headroom under the tightest true positive (0.066) and
// 5x over the Latin tie (0.006).
const AUTO_GATE = 0.95;
const AUTO_MARGIN = 0.03;

// A margin this large is not a close call, so stop probing rather than pay for
// the remaining alphabets. Hindi-as-Devanagari measured +0.426 over the default,
// so it settles on the first candidate instead of trying all nine -- which is
// exactly the case where each probe is most expensive, because the models still
// have to be fetched. The near-ties (Greek +0.066, Cyrillic +0.104) fall below
// this and are still decided by comparing every candidate.
const AUTO_DECISIVE = 0.25;

/** Candidates to probe, cheapest-first. Only alphabets the app already serves. */
// Ordered by how likely they are to be the answer, because the early exit above
// rewards finding a decisive winner sooner.
const AUTO_CANDIDATES = [
  'devanagari',
  'arabic',
  'cyrillic',
  'korean',
  'th',
  'ta',
  'te',
  'el',
  'eslav',
];

const meanConfidence = (p: NainaPage | null): number => {
  if (!p || p.lines.length === 0) return 0;
  return p.lines.reduce((a, l) => a + l.confidence, 0) / p.lines.length;
};

self.addEventListener('message', async (event: MessageEvent<WorkerRequest>) => {
  const req = event.data;
  try {
    if (req.kind === 'warm') {
      const reader = await readerFor(req.tier, req.language, req.id);
      post({ id: req.id, kind: 'warmed', tier: req.tier, version: reader.version() });
      return;
    }

    const auto = req.language === 'auto';
    let lang = auto ? '' : req.language;

    let reader = await readerFor(req.tier, lang, req.id);
    post({ id: req.id, kind: 'stage', label: 'Reading' });

    const started = performance.now();
    let markdown = await reader.readMarkdown(req.rgb, req.width, req.height);
    let page = await reader.readJson(req.rgb, req.width, req.height);
    let detected = false;

    if (auto && meanConfidence(page) < AUTO_GATE) {
      // The default read looks weak, so this may be the wrong alphabet. Try the
      // others on the same image and keep the best, but only if it clears the
      // default by MARGIN -- see the note above on why a plain argmax is wrong.
      // Probe on the downscaled copy when one was supplied. Only the RELATIVE
      // ordering of alphabets matters here, and full-resolution probing cost
      // 130s on a large page against a few seconds at 900px.
      const pr = req.probe ?? { rgb: req.rgb, width: req.width, height: req.height };

      // The baseline must be measured on the same image the candidates are, or
      // the margin compares two different things.
      const baseRead = req.probe
        ? await reader.readJson(pr.rgb, pr.width, pr.height)
        : page;
      const baseline = meanConfidence(baseRead);
      let best = { lang: '', score: baseline };

      for (const cand of AUTO_CANDIDATES) {
        post({ id: req.id, kind: 'stage', label: `Trying ${cand}` });
        try {
          const r = await readerFor(req.tier, cand, req.id);
          const score = meanConfidence(await r.readJson(pr.rgb, pr.width, pr.height));
          if (score > best.score) best = { lang: cand, score };
          if (score >= baseline + AUTO_DECISIVE) break;
        } catch {
          // A candidate whose weights are not served is skipped rather than
          // failing the whole read.
        }
      }

      if (best.lang !== '' && best.score >= baseline + AUTO_MARGIN) {
        // Re-read at full resolution with the winner; the probe was only ever a
        // vote, never the answer that gets returned.
        post({ id: req.id, kind: 'stage', label: `Reading as ${best.lang}` });
        const winner = await readerFor(req.tier, best.lang, req.id);
        markdown = await winner.readMarkdown(req.rgb, req.width, req.height);
        page = await winner.readJson(req.rgb, req.width, req.height);
        lang = best.lang;
        detected = true;
      }
    }

    const ms = performance.now() - started;
    if (!markdown && !page) {
      post({ id: req.id, kind: 'error', message: reader.lastError() });
      return;
    }
    post({ id: req.id, kind: 'result', markdown, page, ms, language: lang, detected });
  } catch (e) {
    post({ id: req.id, kind: 'error', message: e instanceof Error ? e.message : String(e) });
  }
});
