// naina web demo.
//
// Everything here is UI. The OCR itself is one call into the WASM core:
//
//   reader.readJson(rgb, width, height)
//
// If you find yourself adding image processing or box decoding to this file, it
// belongs in the C++ core instead — keeping it there is what makes the browser
// and the server produce the same answers.

import { createReader } from '@jvoltci/naina-wasm';
import './styles.css';

type Line = {
  text: string;
  confidence: number;
  score: number;
  region_id: number;
  quad: number[]; // x0,y0,x1,y1,x2,y2,x3,y3 in source pixels
};
type Region = { kind: string; order: number; bbox: number[] };
type Page = { lines: Line[]; regions: Region[] };
type Reader = Awaited<ReturnType<typeof createReader>>;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const dropEl = $<HTMLElement>('drop');
const fileEl = $<HTMLInputElement>('file');
const tierEl = $<HTMLSelectElement>('tier');
const boxesEl = $<HTMLInputElement>('show-boxes');
const statusEl = $<HTMLElement>('status');
const statusTextEl = $<HTMLElement>('status-text');
const barFillEl = $<HTMLElement>('bar-fill');
const resultsEl = $<HTMLElement>('results');
const canvasEl = $<HTMLCanvasElement>('canvas');
const outputEl = $<HTMLElement>('output');
const metaEl = $<HTMLElement>('meta');
const errorEl = $<HTMLElement>('error');
const tabMdEl = $<HTMLButtonElement>('tab-md');
const tabJsonEl = $<HTMLButtonElement>('tab-json');
const copyEl = $<HTMLButtonElement>('copy');
const downloadEl = $<HTMLButtonElement>('download');
const offlineBadgeEl = $<HTMLElement>('offline-badge');

// One reader per tier, kept alive: creating one downloads weights, so returning
// to a tier already used should cost nothing.
const readers = new Map<string, Reader>();

let lastMarkdown = '';
let lastJson: Page | null = null;
let lastBitmap: ImageBitmap | null = null;
let view: 'md' | 'json' = 'md';

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

/**
 * Decode at native resolution and hand naina the raw pixels.
 *
 * Deliberately no scaling here. Canvas scaling uses a browser-defined filter —
 * the HTML spec leaves it implementation-defined and Chrome, Safari and Firefox
 * differ — so resizing here would make the result depend on the browser. naina's
 * own resize is the same code on every platform.
 */
function toRgb(bitmap: ImageBitmap): Uint8Array {
  const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('could not get a 2D canvas context');
  ctx.drawImage(bitmap, 0, 0);
  const { data } = ctx.getImageData(0, 0, bitmap.width, bitmap.height);

  const rgb = new Uint8Array(bitmap.width * bitmap.height * 3);
  for (let i = 0, j = 0; i < data.length; i += 4, j += 3) {
    rgb[j] = data[i];
    rgb[j + 1] = data[i + 1];
    rgb[j + 2] = data[i + 2];
  }
  return rgb;
}

async function readerFor(tier: string): Promise<Reader> {
  const existing = readers.get(tier);
  if (existing) return existing;

  const mb = tier === 'tiny' ? 11 : 54;
  setStatus(`Fetching the ${tier} model — about ${mb} MB, once…`, 0);

  const reader = await createReader({
    tier: tier as 'tiny' | 'small',
    onProgress: (done: number, total: number, path: string) => {
      const name = path.slice(path.lastIndexOf('/') + 1).replace(/^[0-9a-f]{16}__/, '');
      setStatus(`Fetching weights — ${name} (${done} of ${total})`, done / total);
    },
  });
  readers.set(tier, reader);
  return reader;
}

function drawOverlay() {
  if (!lastBitmap) return;

  // Cap the backing store so a 4000px scan does not allocate an enormous
  // canvas. This affects only what is drawn, never what naina read.
  const maxSide = 1400;
  const scale = Math.min(1, maxSide / Math.max(lastBitmap.width, lastBitmap.height));
  canvasEl.width = Math.round(lastBitmap.width * scale);
  canvasEl.height = Math.round(lastBitmap.height * scale);

  const ctx = canvasEl.getContext('2d');
  if (!ctx) return;
  ctx.drawImage(lastBitmap, 0, 0, canvasEl.width, canvasEl.height);

  if (!boxesEl.checked || !lastJson) return;

  for (const line of lastJson.lines) {
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

function renderOutput() {
  tabMdEl.classList.toggle('is-active', view === 'md');
  tabJsonEl.classList.toggle('is-active', view === 'json');
  outputEl.textContent =
    view === 'md' ? lastMarkdown : lastJson ? JSON.stringify(lastJson, null, 2) : '';
}

async function read(bitmap: ImageBitmap) {
  errorEl.hidden = true;

  const reader = await readerFor(tierEl.value);
  setStatus('Reading the page…', null);
  const rgb = toRgb(bitmap);

  const started = performance.now();
  const markdown = await reader.readMarkdown(rgb, bitmap.width, bitmap.height);
  const page = (await reader.readJson(rgb, bitmap.width, bitmap.height)) as Page | null;
  const elapsed = performance.now() - started;

  if (!markdown && !page) {
    showError(`naina could not read that page: ${reader.lastError()}`);
    return;
  }

  lastBitmap = bitmap;
  lastMarkdown = markdown;
  lastJson = page;

  const lines = page?.lines.length ?? 0;
  const regions = page?.regions.length ?? 0;
  const mean = lines && page ? page.lines.reduce((a, l) => a + l.confidence, 0) / lines : 0;
  metaEl.textContent =
    `${bitmap.width}×${bitmap.height} · ${lines} lines · ${regions} regions · ` +
    `mean confidence ${mean.toFixed(2)} · ${(elapsed / 1000).toFixed(1)}s`;

  clearStatus();
  resultsEl.hidden = false;
  drawOverlay();
  renderOutput();
}

async function handleFile(file: File | Blob) {
  errorEl.hidden = true;
  let bitmap: ImageBitmap;
  try {
    bitmap = await createImageBitmap(file);
  } catch {
    showError('That file could not be decoded as an image.');
    return;
  }
  try {
    await read(bitmap);
  } catch (e) {
    showError(e instanceof Error ? e.message : String(e));
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
  const f = fileEl.files?.[0];
  if (f) void handleFile(f);
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
  const f = e.dataTransfer?.files?.[0];
  if (f) void handleFile(f);
});

// Paste anywhere on the page — the fastest path for a screenshot.
window.addEventListener('paste', (e) => {
  const item = Array.from(e.clipboardData?.items ?? []).find((i) =>
    i.type.startsWith('image/'),
  );
  const blob = item?.getAsFile();
  if (blob) void handleFile(blob);
});

boxesEl.addEventListener('change', drawOverlay);
tabMdEl.addEventListener('click', () => {
  view = 'md';
  renderOutput();
});
tabJsonEl.addEventListener('click', () => {
  view = 'json';
  renderOutput();
});

copyEl.addEventListener('click', async () => {
  const text = view === 'md' ? lastMarkdown : JSON.stringify(lastJson, null, 2);
  await navigator.clipboard.writeText(text);
  copyEl.textContent = 'Copied';
  setTimeout(() => {
    copyEl.textContent = 'Copy';
  }, 1200);
});

downloadEl.addEventListener('click', () => {
  const md = view === 'md';
  const blob = new Blob([md ? lastMarkdown : JSON.stringify(lastJson, null, 2)], {
    type: md ? 'text/markdown' : 'application/json',
  });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = md ? 'page.md' : 'page.json';
  a.click();
  URL.revokeObjectURL(a.href);
});

// Switching tier invalidates what is on screen, since it was read by the other
// model. Re-read the same image rather than leaving a stale result beside a new
// label.
tierEl.addEventListener('change', () => {
  if (lastBitmap) void read(lastBitmap).catch((e: unknown) => showError(String(e)));
});

// ── offline ────────────────────────────────────────────────────────────

// The service worker caches the app shell and the wasm. Model weights are
// cached separately by the runtime, keyed on their immutable release URLs.
if ('serviceWorker' in navigator && import.meta.env.PROD) {
  window.addEventListener('load', () => {
    navigator.serviceWorker
      .register(`${import.meta.env.BASE_URL}sw.js`)
      .then(() => {
        offlineBadgeEl.hidden = false;
      })
      .catch(() => {
        // Offline support is a bonus; the app works without it.
      });
  });
}
