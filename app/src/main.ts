// naina — the web app.
//
// This file is UI and orchestration only. OCR happens in ocr.worker.ts, and the
// reading logic itself lives in the C++ core. If image processing or box
// decoding shows up here, it belongs in the core instead — that is what keeps
// this tool and naina's Python/Node packages producing the same answers.

import type { NainaPage, TierName } from '@jvoltci/naina-wasm';
import { toPages, toRgb, type SourcePage } from './pages';
import type { WorkerRequest, WorkerResponse } from './ocr.worker';
import './styles.css';

interface Result {
  page: SourcePage;
  markdown: string;
  json: NainaPage | null;
  ms: number;
  error?: string;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const dropEl = $<HTMLElement>('drop');
const fileEl = $<HTMLInputElement>('file');
const tierEl = $<HTMLSelectElement>('tier');
const langEl = $<HTMLSelectElement>('language');
const boxesEl = $<HTMLInputElement>('show-boxes');
const statusEl = $<HTMLElement>('status');
const statusTextEl = $<HTMLElement>('status-text');
const barFillEl = $<HTMLElement>('bar-fill');
const resultsEl = $<HTMLElement>('results');
const pagerEl = $<HTMLElement>('pager');
const canvasEl = $<HTMLCanvasElement>('canvas');
const outputEl = $<HTMLElement>('output');
const metaEl = $<HTMLElement>('meta');
const errorEl = $<HTMLElement>('error');
const tabMdEl = $<HTMLButtonElement>('tab-md');
const tabTxtEl = $<HTMLButtonElement>('tab-txt');
const tabJsonEl = $<HTMLButtonElement>('tab-json');
const copyEl = $<HTMLButtonElement>('copy');
const downloadEl = $<HTMLButtonElement>('download');
const downloadAllEl = $<HTMLButtonElement>('download-all');
const offlineBadgeEl = $<HTMLElement>('offline-badge');

const worker = new Worker(new URL('./ocr.worker.ts', import.meta.url), { type: 'module' });

let results: Result[] = [];
let current = 0;
let view: 'md' | 'txt' | 'json' = 'md';
let nextId = 1;
let busy = false;

// Remember the model choice: a returning user should not have to re-pick it, and
// the weights for it are already cached.
const SAVED_TIER = 'naina.tier';
const SAVED_LANG = 'naina.language';
const savedTier = localStorage.getItem(SAVED_TIER);
if (savedTier && [...tierEl.options].some((o) => o.value === savedTier)) {
  tierEl.value = savedTier;
}
const savedLang = localStorage.getItem(SAVED_LANG);
if (savedLang !== null && [...langEl.options].some((o) => o.value === savedLang)) {
  langEl.value = savedLang;
}

// ── worker plumbing ────────────────────────────────────────────────────

const pending = new Map<
  number,
  { resolve: (r: WorkerResponse) => void; reject: (e: Error) => void }
>();

worker.addEventListener('message', (event: MessageEvent<WorkerResponse>) => {
  const msg = event.data;

  // Progress and stage are broadcasts, not replies — they arrive many times per
  // request and must not settle the promise.
  if (msg.kind === 'progress') {
    setStatus(`Fetching weights — ${msg.name} (${msg.done} of ${msg.total})`, msg.done / msg.total);
    return;
  }
  if (msg.kind === 'stage') {
    setStatus(`${msg.label}…`, null);
    return;
  }

  const waiter = pending.get(msg.id);
  if (!waiter) return;
  pending.delete(msg.id);
  if (msg.kind === 'error') waiter.reject(new Error(msg.message));
  else waiter.resolve(msg);
});

worker.addEventListener('error', (e) => showError(`The OCR worker failed: ${e.message}`));

// Omit over a union collapses to the keys the members share, which would drop
// every field that distinguishes a 'read' from a 'warm'. Distributing keeps each
// member's own shape.
type RequestBody = WorkerRequest extends infer T
  ? T extends { id: number }
    ? Omit<T, 'id'>
    : never
  : never;

function ask(req: RequestBody): Promise<WorkerResponse> {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    worker.postMessage({ ...req, id } as WorkerRequest);
  });
}

// ── status ─────────────────────────────────────────────────────────────

function setStatus(text: string, fraction: number | null) {
  statusEl.hidden = false;
  statusTextEl.textContent = text;
  if (fraction === null) {
    barFillEl.style.width = '100%';
    barFillEl.classList.add('is-indeterminate');
  } else {
    barFillEl.classList.remove('is-indeterminate');
    barFillEl.style.width = `${Math.round(fraction * 100)}%`;
  }
}

function clearStatus() {
  statusEl.hidden = true;
  barFillEl.classList.remove('is-indeterminate');
}

function showError(message: string) {
  clearStatus();
  errorEl.hidden = false;
  errorEl.textContent = message;
}

// ── rendering ──────────────────────────────────────────────────────────

/** Every line in reading order, no markdown syntax. */
function toPlainText(page: NainaPage | null): string {
  if (!page) return '';
  return page.lines.map((l) => l.text).join('\n');
}

function drawOverlay() {
  const r = results[current];
  if (!r) return;
  const { bitmap } = r.page;

  // Cap the backing store so a 3000px scan does not allocate an enormous
  // canvas. This affects only what is drawn, never what naina read.
  const maxSide = 1400;
  const scale = Math.min(1, maxSide / Math.max(bitmap.width, bitmap.height));
  canvasEl.width = Math.round(bitmap.width * scale);
  canvasEl.height = Math.round(bitmap.height * scale);

  const ctx = canvasEl.getContext('2d');
  if (!ctx) return;
  ctx.drawImage(bitmap, 0, 0, canvasEl.width, canvasEl.height);

  if (!boxesEl.checked || !r.json) return;

  for (const line of r.json.lines) {
    const q = line.quad;
    if (q.length < 8) continue;

    // Confidence drives the hue — green when sure, amber when not — so it is
    // obvious at a glance which lines to distrust.
    const t = Math.max(0, Math.min(1, (line.confidence - 0.5) / 0.5));
    const hue = Math.round(120 * t);
    ctx.strokeStyle = `hsl(${hue} 85% 55% / 0.95)`;
    ctx.fillStyle = `hsl(${hue} 85% 55% / 0.10)`;
    ctx.lineWidth = 1.5;

    ctx.beginPath();
    ctx.moveTo(q[0] * scale, q[1] * scale);
    for (let i = 2; i < 8; i += 2) ctx.lineTo(q[i] * scale, q[i + 1] * scale);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }
}

function currentText(): string {
  const r = results[current];
  if (!r) return '';
  if (view === 'md') return r.markdown;
  if (view === 'txt') return toPlainText(r.json);
  return r.json ? JSON.stringify(r.json, null, 2) : '';
}

function renderPager() {
  pagerEl.hidden = results.length < 2;
  if (results.length < 2) return;

  pagerEl.replaceChildren(
    ...results.map((r, i) => {
      const b = document.createElement('button');
      b.type = 'button';
      b.className = `page-chip${i === current ? ' is-active' : ''}${r.error ? ' is-failed' : ''}`;
      b.textContent = String(r.page.number);
      b.title = r.error ? `${r.page.label} — ${r.error}` : r.page.label;
      b.addEventListener('click', () => {
        current = i;
        render();
      });
      return b;
    }),
  );
}

function render() {
  resultsEl.hidden = results.length === 0;
  const r = results[current];
  if (!r) return;

  tabMdEl.classList.toggle('is-active', view === 'md');
  tabTxtEl.classList.toggle('is-active', view === 'txt');
  tabJsonEl.classList.toggle('is-active', view === 'json');
  outputEl.textContent = r.error ? `Could not read this page: ${r.error}` : currentText();

  const lines = r.json?.lines.length ?? 0;
  const regions = r.json?.regions.length ?? 0;
  const mean = lines && r.json ? r.json.lines.reduce((a, l) => a + l.confidence, 0) / lines : 0;
  metaEl.textContent =
    `${r.page.label} · ${r.page.bitmap.width}×${r.page.bitmap.height} · ` +
    `${lines} lines · ${regions} regions · mean confidence ${mean.toFixed(2)} · ` +
    `${(r.ms / 1000).toFixed(1)}s`;

  downloadAllEl.hidden = results.length < 2;
  renderPager();
  drawOverlay();
}

// ── reading ────────────────────────────────────────────────────────────

async function readPages(
  pages: SourcePage[],
  tier: TierName,
  language: string,
  verb: string,
) {
  results = [];
  current = 0;

  for (const [i, page] of pages.entries()) {
    if (pages.length > 1) {
      setStatus(`${verb} page ${i + 1} of ${pages.length}…`, i / pages.length);
    }
    try {
      const res = await ask({
        kind: 'read',
        tier,
        language,
        rgb: toRgb(page.bitmap),
        width: page.bitmap.width,
        height: page.bitmap.height,
      });
      if (res.kind !== 'result') throw new Error('unexpected reply from the OCR worker');
      results.push({ page, markdown: res.markdown, json: res.page, ms: res.ms });
    } catch (e) {
      results.push({
        page,
        markdown: '',
        json: null,
        ms: 0,
        error: e instanceof Error ? e.message : String(e),
      });
    }
    // Paint after each page so a long PDF shows progress rather than nothing.
    render();
  }

  clearStatus();
  const failed = results.filter((r) => r.error).length;
  if (failed === results.length && failed > 0) {
    showError(`No page could be read. ${results[0].error ?? ''}`);
  } else if (failed > 0) {
    showError(`${failed} of ${results.length} pages could not be read; the rest are below.`);
  }
}

async function handleFiles(files: (File | Blob)[]) {
  // Serialising is deliberate: a second read while weights are still downloading
  // would double the traffic and race on the same reader.
  if (busy) return;
  busy = true;
  errorEl.hidden = true;

  try {
    const tier = tierEl.value as TierName;
    const language = langEl.value;
    setStatus('Opening…', null);

    const pages: SourcePage[] = [];
    for (const file of files) {
      pages.push(
        ...(await toPages(file, (n, of) => setStatus(`Rendering page ${n} of ${of}…`, n / of))),
      );
    }
    if (pages.length === 0) {
      showError('Nothing readable in that file.');
      return;
    }

    await readPages(pages, tier, language, 'Reading');
    localStorage.setItem(SAVED_TIER, tier);
    localStorage.setItem(SAVED_LANG, language);
  } catch (e) {
    showError(e instanceof Error ? e.message : String(e));
  } finally {
    busy = false;
  }
}

// ── wiring ─────────────────────────────────────────────────────────────

dropEl.addEventListener('click', () => fileEl.click());
dropEl.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' || e.key === ' ') {
    e.preventDefault();
    fileEl.click();
  }
});
fileEl.addEventListener('change', () => {
  const files = Array.from(fileEl.files ?? []);
  if (files.length) void handleFiles(files);
});

for (const type of ['dragenter', 'dragover'] as const) {
  dropEl.addEventListener(type, (e) => {
    e.preventDefault();
    dropEl.classList.add('is-over');
  });
}
for (const type of ['dragleave', 'drop'] as const) {
  dropEl.addEventListener(type, () => dropEl.classList.remove('is-over'));
}
dropEl.addEventListener('drop', (e) => {
  e.preventDefault();
  const files = Array.from(e.dataTransfer?.files ?? []);
  if (files.length) void handleFiles(files);
});

// Paste anywhere — the fastest path for a screenshot.
window.addEventListener('paste', (e) => {
  const blobs = Array.from(e.clipboardData?.items ?? [])
    .filter((i) => i.type.startsWith('image/'))
    .map((i) => i.getAsFile())
    .filter((b): b is File => b !== null);
  if (blobs.length) void handleFiles(blobs);
});

boxesEl.addEventListener('change', drawOverlay);
for (const [el, mode] of [
  [tabMdEl, 'md'],
  [tabTxtEl, 'txt'],
  [tabJsonEl, 'json'],
] as const) {
  el.addEventListener('click', () => {
    view = mode;
    render();
  });
}

copyEl.addEventListener('click', async () => {
  await navigator.clipboard.writeText(currentText());
  copyEl.textContent = 'Copied';
  setTimeout(() => {
    copyEl.textContent = 'Copy';
  }, 1200);
});

function save(text: string, filename: string, type: string) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type }));
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

downloadEl.addEventListener('click', () => {
  const ext = view === 'json' ? 'json' : view === 'txt' ? 'txt' : 'md';
  const type =
    view === 'json' ? 'application/json' : view === 'txt' ? 'text/plain' : 'text/markdown';
  save(currentText(), `page-${results[current]?.page.number ?? 1}.${ext}`, type);
});

// One file for a whole document, pages separated by a rule — what you actually
// want after reading a 20-page scan.
downloadAllEl.addEventListener('click', () => {
  const joined = results
    .map((r) => {
      const body = r.error ? `> Could not read this page: ${r.error}` : r.markdown;
      return `<!-- ${r.page.label} -->\n\n${body}`;
    })
    .join('\n\n---\n\n');
  save(joined, 'document.md', 'text/markdown');
});

// Switching model invalidates what is on screen, since it was read by the other
// one. Re-read rather than showing a stale result under a new label.
function reReadOnSelectorChange() {
  localStorage.setItem(SAVED_TIER, tierEl.value);
  localStorage.setItem(SAVED_LANG, langEl.value);
  if (!results.length || busy) return;
  busy = true;
  const pages = results.map((r) => r.page);
  void readPages(pages, tierEl.value as TierName, langEl.value, 'Re-reading').finally(
    () => {
      busy = false;
    },
  );
}

tierEl.addEventListener('change', reReadOnSelectorChange);
langEl.addEventListener('change', reReadOnSelectorChange);

// ── offline ────────────────────────────────────────────────────────────

// The service worker caches the app shell and naina.wasm. Model weights are
// cached separately by naina's runtime, keyed on immutable release URLs.
if ('serviceWorker' in navigator && import.meta.env.PROD) {
  window.addEventListener('load', () => {
    navigator.serviceWorker
      .register(`${import.meta.env.BASE_URL}sw.js`)
      .then(() => {
        offlineBadgeEl.hidden = false;
      })
      .catch(() => {
        // Offline is a bonus; the tool works without it.
      });
  });
}
