import './styles.css';
import * as ort from 'onnxruntime-web';
import { Engine } from './engine';
import type { FrameResult } from './types';

// ── ORT runtime config ──────────────────────────────────────────────────
// onnxruntime-web ≥1.18 resolves its own wasm assets through the bundler
// (Vite hashes them into dist/assets/), so we don't set wasmPaths. We
// only tune the thread count for the WASM EP.
ort.env.wasm.numThreads = Math.min(4, navigator.hardwareConcurrency || 1);

// ── DOM refs ────────────────────────────────────────────────────────────
const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const video      = $<HTMLVideoElement>('video');
const overlay    = $<HTMLCanvasElement>('overlay');
const statusEl   = $<HTMLDivElement>('status');
const startBtn   = $<HTMLButtonElement>('start-btn');
const stopBtn    = $<HTMLButtonElement>('stop-btn');
const enrolBtn   = $<HTMLButtonElement>('enrol-btn');
const enrolName  = $<HTMLInputElement>('enrol-name');
const continuous = $<HTMLInputElement>('continuous');
const thresholdR = $<HTMLInputElement>('threshold');
const thresholdV = $<HTMLSpanElement>('threshold-value');
const galleryUl  = $<HTMLUListElement>('gallery-list');
const clearBtn   = $<HTMLButtonElement>('clear-btn');
const mFaces     = $<HTMLElement>('m-faces');
const mDetect    = $<HTMLElement>('m-detect');
const mEmbed     = $<HTMLElement>('m-embed');
const mFps       = $<HTMLElement>('m-fps');
const mBackend   = $<HTMLElement>('m-backend');

const ctx = overlay.getContext('2d')!;

// ── state ───────────────────────────────────────────────────────────────
let engine: Engine | null = null;
let stream: MediaStream | null = null;
let rafId = 0;
let lastFrameTs = 0;
let fpsEMA = 0;
let selectedFaceIdx = -1;
let lastFrameResult: FrameResult | null = null;
let consecutiveErrors = 0;
const MAX_CONSECUTIVE_ERRORS = 5;

threshold(); // init label

// ── boot ────────────────────────────────────────────────────────────────
(async function init() {
  setStatus('Loading models (~40 MB, first run only)…');
  try {
    engine = await Engine.create({
      modelBase: `${import.meta.env.BASE_URL}models`,
      // OpenCV Zoo's YuNet 2023mar has a fixed 640x640 input. Don't
      // change this unless you swap to a dynamic-input variant.
      detectorInputSize: 640,
      confidenceThreshold: 0.6,
      nmsIouThreshold: 0.3,
    });
    mBackend.textContent = engine.backend;
    setStatus('Models loaded. Click Start to begin.');
    refreshGalleryUI();
  } catch (err) {
    console.error(err);
    setStatus(`Failed to load models: ${(err as Error).message}`);
  }
})();

// ── controls ────────────────────────────────────────────────────────────
startBtn.addEventListener('click', startCamera);
stopBtn .addEventListener('click', stopCamera);
clearBtn.addEventListener('click', () => { engine?.gallery.clear(); refreshGalleryUI(); });
thresholdR.addEventListener('input', threshold);
enrolBtn.addEventListener('click', enrolSelected);
overlay.addEventListener('click', onOverlayClick);

function threshold() {
  thresholdV.textContent = (+thresholdR.value).toFixed(2);
}

async function startCamera() {
  if (!engine) { setStatus('Engine not ready yet.'); return; }
  try {
    stream = await navigator.mediaDevices.getUserMedia({
      video: { width: { ideal: 1280 }, height: { ideal: 720 }, facingMode: 'user' },
      audio: false,
    });
  } catch (err) {
    setStatus(`Camera permission denied or unavailable: ${(err as Error).message}`);
    return;
  }

  video.srcObject = stream;
  await video.play();
  syncCanvasToVideo();

  startBtn.disabled = true;
  stopBtn.disabled  = false;
  setStatus('Running…');
  lastFrameTs = performance.now();
  fpsEMA = 0;
  loop();
}

function stopCamera() {
  cancelAnimationFrame(rafId);
  rafId = 0;
  if (stream) {
    stream.getTracks().forEach(t => t.stop());
    stream = null;
  }
  video.srcObject = null;
  startBtn.disabled = false;
  stopBtn.disabled  = true;
  setStatus('Stopped.');
  selectedFaceIdx = -1;
  enrolBtn.disabled = true;
  ctx.clearRect(0, 0, overlay.width, overlay.height);
}

function syncCanvasToVideo() {
  overlay.width  = video.videoWidth  || 1280;
  overlay.height = video.videoHeight || 720;
}

// ── main loop ───────────────────────────────────────────────────────────
async function loop() {
  if (!engine || !stream) return;

  const now = performance.now();
  const dt = now - lastFrameTs;
  lastFrameTs = now;
  const fps = 1000 / Math.max(1, dt);
  fpsEMA = fpsEMA === 0 ? fps : 0.9 * fpsEMA + 0.1 * fps;

  let result: FrameResult;
  try {
    if (continuous.checked) {
      result = await engine.recognizeFrame(video, +thresholdR.value);
    } else {
      const faces = await engine.detectFaces(video);
      result = {
        faces,
        matches: faces.map(() => ({ identityId: null, name: 'unknown', similarity: 0 })),
        timings: { detectMs: 0, embedMsPerFace: 0 },
      };
    }
  } catch (err) {
    consecutiveErrors++;
    console.error(err);
    setStatus(`Inference error (${consecutiveErrors}/${MAX_CONSECUTIVE_ERRORS}): ${(err as Error).message}`);
    if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
      stopCamera();
      setStatus(`Stopped after ${MAX_CONSECUTIVE_ERRORS} consecutive errors. Try refreshing the page.`);
      return;
    }
    // Re-draw last good result so bbox doesn't flicker on transient errors.
    syncCanvasToVideo();
    if (lastFrameResult) draw(lastFrameResult);
    rafId = requestAnimationFrame(loop);
    return;
  }

  consecutiveErrors = 0;
  lastFrameResult = result;

  // Clamp selection if face count changed.
  if (selectedFaceIdx >= result.faces.length) selectedFaceIdx = -1;

  syncCanvasToVideo();
  draw(result);
  updateMetrics(result, fpsEMA);

  rafId = requestAnimationFrame(loop);
}

// ── drawing ─────────────────────────────────────────────────────────────
function draw(result: FrameResult) {
  ctx.clearRect(0, 0, overlay.width, overlay.height);
  ctx.lineWidth = 3;
  ctx.font = '600 16px ui-sans-serif, system-ui, sans-serif';
  ctx.textBaseline = 'bottom';

  result.faces.forEach((face, i) => {
    const match = result.matches[i];
    const isSelected = i === selectedFaceIdx;
    const recognized = match.identityId !== null;
    const color = isSelected ? '#5eead4'
                : recognized ? '#34d399'
                :              '#f59e0b';

    // bbox
    ctx.strokeStyle = color;
    ctx.strokeRect(face.bbox.x, face.bbox.y, face.bbox.w, face.bbox.h);

    // landmarks
    ctx.fillStyle = color;
    for (const p of face.landmarks) {
      ctx.beginPath();
      ctx.arc(p.x, p.y, 2.5, 0, Math.PI * 2);
      ctx.fill();
    }

    // label
    const label = recognized
      ? `${match.name} · ${(match.similarity * 100).toFixed(0)}%`
      : (engine!.gallery.size() > 0
          ? `unknown · ${(match.similarity * 100).toFixed(0)}%`
          : `face #${i + 1}`);
    drawLabel(face.bbox.x, face.bbox.y, label, color);
  });
}

function drawLabel(x: number, y: number, text: string, color: string) {
  const pad = 4;
  const metrics = ctx.measureText(text);
  const w = metrics.width + pad * 2;
  const h = 22;
  // The video and overlay are flipped via CSS (scaleX(-1)) so the user
  // sees a mirror image. We draw labels in canvas coords, then flip the
  // text horizontally so it reads correctly on screen.
  ctx.save();
  ctx.translate(x, Math.max(h, y));
  ctx.scale(-1, 1);                 // unflip text horizontally
  ctx.fillStyle = color;
  ctx.fillRect(-w, -h, w, h);
  ctx.fillStyle = '#0a0f2c';
  ctx.fillText(text, -w + pad, -pad);
  ctx.restore();
}

function updateMetrics(result: FrameResult, fps: number) {
  mFaces.textContent  = String(result.faces.length);
  mDetect.textContent = `${result.timings.detectMs.toFixed(1)} ms`;
  mEmbed.textContent  = result.faces.length
    ? `${result.timings.embedMsPerFace.toFixed(1)} ms`
    : '— ms';
  mFps.textContent    = `${fps.toFixed(1)} fps`;
}

function setStatus(msg: string) { statusEl.textContent = msg; }

// ── selection + enrolment ───────────────────────────────────────────────
function onOverlayClick(ev: MouseEvent) {
  if (!lastFrameResult) return;
  const rect = overlay.getBoundingClientRect();
  // Canvas drawing is in unflipped coords. The CSS scaleX(-1) means the
  // user sees pixel column W-1-clickX. Translate accordingly.
  const cx = (overlay.width  / rect.width)  * (rect.width  - (ev.clientX - rect.left));
  const cy = (overlay.height / rect.height) * (ev.clientY - rect.top);

  const hit = lastFrameResult.faces.findIndex(f =>
    cx >= f.bbox.x && cx <= f.bbox.x + f.bbox.w &&
    cy >= f.bbox.y && cy <= f.bbox.y + f.bbox.h);

  selectedFaceIdx = hit;
  enrolBtn.disabled = hit < 0;
  if (lastFrameResult) draw(lastFrameResult);
}

async function enrolSelected() {
  if (!engine || selectedFaceIdx < 0 || !lastFrameResult) return;
  const name = enrolName.value.trim();
  if (!name) { setStatus('Type a name first.'); return; }

  // Snapshot the *current* video frame at the time of the click,
  // because the live frame may have moved on.
  const face = lastFrameResult.faces[selectedFaceIdx];
  try {
    const emb = await engine.embedFace(video, face);
    engine.gallery.enrol(name, emb);
    enrolName.value = '';
    refreshGalleryUI();
    setStatus(`Enrolled ${name}. Now showing live recognition.`);
  } catch (err) {
    setStatus(`Enrolment failed: ${(err as Error).message}`);
  }
}

function refreshGalleryUI() {
  if (!engine) return;
  galleryUl.innerHTML = '';
  const list = engine.gallery.list();
  if (list.length === 0) {
    const li = document.createElement('li');
    li.style.color = 'var(--fg-dim)';
    li.style.fontStyle = 'italic';
    li.textContent = 'No one enrolled yet';
    galleryUl.appendChild(li);
    return;
  }
  for (const item of list) {
    const li = document.createElement('li');
    const name = document.createElement('span');
    name.textContent = item.name;
    const pill = document.createElement('span');
    pill.className = 'pill';
    pill.textContent = `${item.embedding.length}-d`;
    const rm = document.createElement('button');
    rm.textContent = 'remove';
    rm.addEventListener('click', () => {
      engine!.gallery.remove(item.id);
      refreshGalleryUI();
    });
    li.append(name, pill, rm);
    galleryUl.appendChild(li);
  }
}
