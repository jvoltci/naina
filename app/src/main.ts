// naina — the web app.
//
// This file is UI and orchestration only. OCR happens in ocr.worker.ts, and the
// reading logic itself lives in the C++ core. If image processing or box
// decoding shows up here, it belongs in the core instead — that is what keeps
// this tool and naina's Python/Node packages producing the same answers.

import type { NainaPage, TierName } from '@jvoltci/naina-wasm';
import { toPages, toProbeBitmap, toRgb, type SourcePage } from './pages';
import type { WorkerRequest, WorkerResponse } from './ocr.worker';
import './styles.css';

interface Result {
  page: SourcePage;
  markdown: string;
  json: NainaPage | null;
  ms: number;
  /** The alphabet that produced this. */
  language?: string;
  /** True when it was detected rather than chosen. */
  detected?: boolean;
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
// Two loaders, not one bar doing both jobs. .n-progress is a real <progress> and
// answers "this much is done"; .n-bar answers "something is happening". nilam's
// loader section calls substituting one for the other a usability error, and the
// single .bar-fill this replaces did exactly that.
const progressEl = $<HTMLProgressElement>('progress');
const barEl = $<HTMLElement>('bar');
const resultsEl = $<HTMLElement>('results');
const skeletonEl = $<HTMLElement>('skeleton');
const pagerEl = $<HTMLElement>('pager');
const canvasEl = $<HTMLCanvasElement>('canvas');
const outputEl = $<HTMLElement>('output');
const metaEl = $<HTMLElement>('meta');
const errorEl = $<HTMLElement>('error');
const errorTextEl = $<HTMLElement>('error-text');
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
    // A stage broadcast knows LESS than the page counter it would replace, so it
    // does not get to replace it. Measured: on a two-page PDF the app showed
    // "Reading page 2 of 2…" with a determinate .n-progress at 50% for a few
    // milliseconds and then overwrote it with "Reading…" and an indeterminate
    // .n-bar, because the worker posts this the moment it starts inferring. The
    // app was throwing away the only proportion it had.
    if (pageStatus) setStatus(pageStatus.text, pageStatus.fraction);
    else setStatus(`${msg.label}…`, null);
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

/**
 * Where a multi-page read has got to, when there is more than one page. Held
 * here rather than passed around because it has to survive the worker's own
 * 'stage' broadcasts, which arrive mid-page and carry no proportion.
 *
 * null for a single page: "page 1 of 1" is not progress, it is arithmetic.
 */
let pageStatus: { text: string; fraction: number } | null = null;

// `fraction` is the one bit of information that decides which loader is honest:
// a number means a proportion is known, null means the duration is not. Nothing
// else in this function branches, deliberately.
function setStatus(text: string, fraction: number | null) {
  statusEl.hidden = false;
  statusTextEl.textContent = text;
  if (fraction === null) {
    progressEl.hidden = true;
    barEl.hidden = false;
  } else {
    barEl.hidden = true;
    progressEl.hidden = false;
    // max="1", so the fraction goes in as-is; <progress> puts it in the
    // accessibility tree itself, which is why there is no aria-valuenow here.
    progressEl.value = fraction;
  }
}

// "Content shaped like THIS is coming." Only worth showing when there is nothing
// on screen yet — on a re-read the previous result is still visible and swapping
// it for grey blocks would lose information the reader already had.
function setSkeleton(on: boolean) {
  skeletonEl.hidden = !on || results.length > 0;
}

function clearStatus() {
  statusEl.hidden = true;
  pageStatus = null;
  setSkeleton(false);
}

function showError(message: string) {
  clearStatus();
  errorEl.hidden = false;
  errorTextEl.textContent = message;
}

// ── rendering ──────────────────────────────────────────────────────────

/** Every line in reading order, no markdown syntax. */
function toPlainText(page: NainaPage | null): string {
  if (!page) return '';
  return page.lines.map((l) => l.text).join('\n');
}

/**
 * Four corner points, whichever shape the core emitted.
 *
 * THE BINDING'S TYPE IS WRONG AND THIS IS WHY THE OVERLAY NEVER PAINTED.
 * bindings/wasm/src/index.d.ts declares `quad: number[]` and documents it as
 * "x0,y0,x1,y1,x2,y2,x3,y3 in source pixels". The runtime actually emits four
 * [x, y] PAIRS: [[105.7,142.2],[1567.1,142.2],[1567.1,201.6],[105.7,201.6]].
 *
 * So the old guard `if (q.length < 8) continue;` was true for every line — a
 * 4-element array — and the loop skipped all of them. TypeScript could not catch
 * it, because the code agreed with the declaration and the declaration disagreed
 * with reality. Found by sampling the painted canvas for non-greyscale pixels:
 * there were zero, on a page whose twelve lines all came back at 0.99.
 *
 * Normalised here rather than in the binding. Correcting a published type
 * declaration is its own change with its own blast radius; this accepts both
 * shapes so it is right either way, and the .d.ts is reported separately.
 */
function toPoints(quad: number[] | number[][]): [number, number][] {
  if (quad.length === 0) return [];
  if (Array.isArray(quad[0])) return quad as [number, number][];
  const flat = quad as number[];
  return Array.from({ length: Math.floor(flat.length / 2) }, (_, i): [number, number] => [
    flat[i * 2],
    flat[i * 2 + 1],
  ]);
}

/**
 * Resolve a nilam token to a concrete colour a canvas will accept.
 *
 * getPropertyValue('--ok-9') returns the token's LITERAL text, which is
 * `light-dark(oklch(…), oklch(…))` — custom properties are substituted, not
 * computed. A canvas has no colour-scheme context, so light-dark() there does
 * not resolve and the assignment is silently dropped. Painting the token onto a
 * real element and reading back its computed `color` does the resolution in the
 * one place that can do it: the DOM.
 */
function resolveToken(name: string): string {
  const probe = document.createElement('span');
  probe.style.position = 'absolute';
  probe.style.visibility = 'hidden';
  probe.style.color = `var(${name})`;
  document.body.append(probe);
  const value = getComputedStyle(probe).color;
  probe.remove();
  return value;
}

/**
 * Does this engine accept color-mix() as a canvas paint? Probed once, because a
 * rejected assignment is a NO-OP rather than a throw — it leaves whatever was
 * there before, which is how a wrong colour ships without anyone noticing.
 *
 * The three literals below are a feature test, not paint: nothing they touch is
 * ever shown. '#000000' is the sentinel the probe watches for, and red/blue are
 * the two simplest arguments color-mix() will accept. A token here would test
 * whether the token resolves, which is a different question and is answered by
 * resolveToken above.
 */
const MIX_OK = (() => {
  const ctx = document.createElement('canvas').getContext('2d');
  if (!ctx) return false;
  ctx.strokeStyle = '#000000';
  ctx.strokeStyle = 'color-mix(in oklab, red 50%, blue)';
  return ctx.strokeStyle !== '#000000';
})();

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

  // THE RAMP'S ENDPOINTS ARE NOW TOKENS, and that is a change of kind rather than
  // of shade. It used to be `hsl(120…0 85% 55%)` — the last two hardcoded colours
  // in src/, justified in styles.css as "data, not decoration", which is a fair
  // carve-out and still leaves two unproven values on the page.
  //
  // --ok-9 and --warn-9 ARE the two things this ramp says: "trust this line" and
  // "check this line". Using them means the overlay inherits everything the
  // palette proved — the separation measured under protanopia, deuteranopia and
  // tritanopia, and the polarity flip, since step 9 is the L 0.585 solid in light
  // mode and the L 0.660 glow in dark. The old literals were one pair of values
  // for both modes, which on a dark page put a 55%-lightness green over a
  // near-black canvas.
  //
  // Resolved per call, not once at module load, so following the OS from light to
  // dark repaints in the right pair.
  //
  // Confidence still drives the mix — green when sure, amber when not — so it is
  // obvious at a glance which lines to distrust. That is the signal; deleting it
  // would remove the only thing telling a reader the model can be wrong.
  const ok = MIX_OK ? resolveToken('--ok-9') : '';
  const warn = MIX_OK ? resolveToken('--warn-9') : '';
  const tokenised = ok !== '' && warn !== '';

  ctx.lineWidth = 1.5;
  for (const line of r.json.lines) {
    const pts = toPoints(line.quad);
    if (pts.length < 3) continue;

    const t = Math.max(0, Math.min(1, (line.confidence - 0.5) / 0.5));
    const paint = tokenised
      ? `color-mix(in oklab, ${ok} ${Math.round(t * 100)}%, ${warn})`
      : // Last resort, and only reachable on an engine without color-mix() in
        // canvas. Kept as the original hsl() ramp rather than a token, because a
        // token cannot be interpolated without the thing that is missing.
        `hsl(${Math.round(120 * t)} 85% 55%)`;
    ctx.strokeStyle = paint;
    ctx.fillStyle = paint;

    ctx.beginPath();
    ctx.moveTo(pts[0][0] * scale, pts[0][1] * scale);
    for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0] * scale, pts[i][1] * scale);
    ctx.closePath();
    // globalAlpha rather than an alpha channel in the colour: color-mix() would
    // need a second nested mix against transparent to carry one, and two
    // different alphas are wanted from the same paint.
    ctx.globalAlpha = 0.1;
    ctx.fill();
    ctx.globalAlpha = 0.95;
    ctx.stroke();
  }
  ctx.globalAlpha = 1;
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
      // .n-btn n-btn-sm is the whole chip. The active one is n-btn-fill, and a
      // failed active one is n-btn-danger — both are nilam's solved step-9/ink
      // pairs, so they invert polarity correctly in dark mode without a literal
      // white anywhere.
      const fill = r.error ? 'n-btn-danger' : 'n-btn-fill';
      b.className = [
        'n-btn',
        'n-btn-sm',
        'page-chip',
        i === current ? fill : '',
        r.error ? 'is-failed' : '',
      ]
        .filter(Boolean)
        .join(' ');
      b.append(String(r.page.number));
      if (r.error) {
        // The non-hue channel. Colour alone made a failed page 3 and a working
        // page 3 the same chip for a deuteranope — WCAG 1.4.1, and the defect
        // nilam's proveStatusChannels() exists to catch.
        const glyph = document.createElement('span');
        glyph.className = 'chip-glyph';
        glyph.setAttribute('aria-hidden', 'true');
        glyph.textContent = '×';
        b.append(glyph);
      }
      // aria-label rather than only title: title is not reliably announced, and
      // for a failed page the reason is the whole point of the chip.
      b.setAttribute('aria-label', r.error ? `${r.page.label} — ${r.error}` : r.page.label);
      if (i === current) b.setAttribute('aria-current', 'page');
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
  // Real content beats a placeholder the moment it exists.
  if (results.length > 0) skeletonEl.hidden = true;
  const r = results[current];
  if (!r) return;

  // aria-selected, not a class. .n-tab selects on the attribute, so a tab whose
  // state is missing from the accessibility tree does not get painted either —
  // which is the point of doing it this way round.
  tabMdEl.setAttribute('aria-selected', String(view === 'md'));
  tabTxtEl.setAttribute('aria-selected', String(view === 'txt'));
  tabJsonEl.setAttribute('aria-selected', String(view === 'json'));
  // --measure is a reading measure; JSON is not read left to right, and at 65ch
  // a nested object wraps mid-key. The exemption is keyed off the content rather
  // than a class someone has to remember to toggle.
  outputEl.dataset.view = view;
  outputEl.textContent = r.error ? `Could not read this page: ${r.error}` : currentText();

  const lines = r.json?.lines.length ?? 0;
  const regions = r.json?.regions.length ?? 0;
  const mean = lines && r.json ? r.json.lines.reduce((a, l) => a + l.confidence, 0) / lines : 0;
  const scriptNote = r.detected
    ? ` · script detected: ${r.language}`
    : r.language
      ? ` · script: ${r.language}`
      : '';
  metaEl.textContent =
    `${r.page.label} · ${r.page.bitmap.width}×${r.page.bitmap.height} · ` +
    `${lines} lines · ${regions} regions · mean confidence ${mean.toFixed(2)} · ` +
    `${(r.ms / 1000).toFixed(1)}s${scriptNote}`;

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
      // i, not i + 1: the bar reports pages FINISHED, which is what a proportion
      // means. Reporting i + 1 would show 50% before the first page had produced
      // anything, and 100% while the last one was still running.
      pageStatus = { text: `${verb} page ${i + 1} of ${pages.length}…`, fraction: i / pages.length };
      setStatus(pageStatus.text, pageStatus.fraction);
    }
    try {
      // Only auto-detection needs the small copy; making one otherwise is waste.
      const probeBitmap = language === 'auto' ? await toProbeBitmap(page.bitmap) : null;
      const res = await ask({
        kind: 'read',
        tier,
        language,
        rgb: toRgb(page.bitmap),
        width: page.bitmap.width,
        height: page.bitmap.height,
        probe: probeBitmap
          ? {
              rgb: toRgb(probeBitmap),
              width: probeBitmap.width,
              height: probeBitmap.height,
            }
          : undefined,
      });
      probeBitmap?.close();
      if (res.kind !== 'result') throw new Error('unexpected reply from the OCR worker');
      results.push({
        page,
        markdown: res.markdown,
        json: res.page,
        ms: res.ms,
        language: res.language,
        detected: res.detected,
      });
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

  // A new file replaces whatever was on screen, so clear it up front rather than
  // leaving a stale page and its stale meta line sitting under a progress bar
  // for the next 40 seconds. This is also what makes the skeleton correct: it is
  // only ever shown when there is genuinely nothing to look at.
  results = [];
  current = 0;
  resultsEl.hidden = true;
  pagerEl.hidden = true;

  try {
    const tier = tierEl.value as TierName;
    const language = langEl.value;
    setStatus('Opening…', null);
    // Before the first result exists the wait is the whole interaction — 27 MB
    // of ort-web, 11 MB of weights, then inference. The skeleton says where the
    // text will land; a bar cannot.
    setSkeleton(true);

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
