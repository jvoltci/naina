// The WASM binding reads a real page correctly.
//
// What this asserts, and why it is not a byte-identity test:
//
// naina's strong guarantee is that its BINDINGS agree — Python, Node and Rust
// over one ONNX Runtime build produce identical output, because they run the
// same compiled core and the same kernels. onnxruntime-web is a different build
// of ONNX Runtime (WASM SIMD rather than native NEON/AVX kernels), so its
// probability maps differ in the last few float bits. Measured on the A4 fixture
// at tiny tier, that moved one marginal blob across DBNet's 0.3 binarize
// threshold and changed line segmentation: 35 native lines against 33 here, 33
// of them character-identical.
//
// Asserting bit-identity across that boundary would be asserting something
// false. So this test asserts what is actually true and load-bearing: the core
// runs in WASM, recognises the page at high confidence, and assembles it into
// the right document structure.
//
// Run:  node bindings/wasm/test/read.test.mjs
//
// Skips loudly, with the reason, when a prerequisite is missing.

import { readFileSync, existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { homedir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

let failures = 0;
function expect(cond, what) {
  if (cond) return;
  console.error(`FAIL: ${what}`);
  failures++;
}
function skip(why) {
  console.log(`SKIP read: ${why}`);
  process.exit(0);
}

if (!existsSync(join(HERE, '..', 'dist', 'naina.mjs'))) {
  skip('dist/naina.mjs not built — run the emcmake build first');
}
try {
  require.resolve('onnxruntime-web');
} catch {
  skip('onnxruntime-web not installed — npm i in bindings/wasm');
}

const fixturePath = process.env.NAINA_WASM_FIXTURE;
if (!fixturePath || !existsSync(fixturePath)) {
  skip('NAINA_WASM_FIXTURE unset or missing (raw: i32 w, i32 h, RGB8)');
}
const tierName = process.env.NAINA_WASM_TIER ?? 'tiny';

const { installBridge, TIER } = await import('../src/index.mjs');
const createNaina = (await import('../dist/naina.mjs')).default;

const Module = await createNaina();
installBridge(Module, { executionProviders: ['wasm'] });

// Stage from the host cache rather than the network: fast, offline, and it
// exercises the same C++ sha256 verification the browser path uses.
const plan = JSON.parse(Module.stagingPlan(TIER[tierName]));
if (plan.length === 0) skip(`registry has no models for tier '${tierName}'`);

const cacheRoot = process.env.NAINA_CACHE ?? join(homedir(), '.cache', 'naina', 'models');
for (const { path } of plan) {
  const host = join(cacheRoot, path.replace(/^\/naina\/models\//, ''));
  if (!existsSync(host)) skip(`weights not in host cache (${host}) — run a native read first`);
  let cur = '';
  for (const part of path.slice(0, path.lastIndexOf('/')).split('/').filter(Boolean)) {
    cur += `/${part}`;
    try {
      Module.FS.mkdir(cur);
    } catch {
      /* exists */
    }
  }
  Module.FS.writeFile(path, readFileSync(host));
}

const raw = readFileSync(fixturePath);
const width = raw.readInt32LE(0);
const height = raw.readInt32LE(4);
const rgb = raw.subarray(8, 8 + width * height * 3);
expect(rgb.length === width * height * 3, 'fixture is not truncated');

const reader = new Module.Reader(TIER[tierName], 0);
expect(reader.ok(), `init succeeds (${Module.statusText(reader.status())})`);
if (!reader.ok()) process.exit(1);

// await: ASYNCIFY makes any export that suspends return a Promise, and
// inference always suspends on ort-web.
const md = await reader.readMarkdown(rgb, width, height);
const page = JSON.parse(await reader.readJson(rgb, width, height));

console.log(`read: ${page.lines.length} line(s), ${md.length} chars of markdown`);

// The fixture is an academic page; these are its actual contents.
for (const phrase of [
  'Deterministic Optical Character Recognition',
  'A. Researcher, B. Coauthor',
  'Abstract',
  'Introduction',
  'tensor execution',
  'Graves et al',
]) {
  expect(md.includes(phrase), `markdown contains ${JSON.stringify(phrase)}`);
}

// Structure, not just text: the title must be an h1.
expect(/^# Deterministic Optical Character Recognition/m.test(md), 'doc title is an h1');

// Recognition quality. PP-OCRv6 on clean rendered serif text should be near
// perfect; a threshold this high would catch a marshalling bug that produced
// plausible-looking but wrong strips.
const confidences = page.lines.map((l) => l.confidence);
const mean = confidences.reduce((a, b) => a + b, 0) / confidences.length;
expect(page.lines.length >= 30, `found >= 30 lines (got ${page.lines.length})`);
expect(mean > 0.95, `mean confidence > 0.95 (got ${mean.toFixed(3)})`);
expect(Math.min(...confidences) > 0.5, `min confidence > 0.5 (got ${Math.min(...confidences).toFixed(3)})`);

// Determinism within this backend: the same input twice must give one answer.
// This is the part of the identity guarantee that DOES hold here, and a
// mismarshalled heap pointer would break it.
const again = await reader.readMarkdown(rgb, width, height);
expect(again === md, 'same input produces identical output on a second run');

reader.delete();

if (failures > 0) {
  console.error(`${failures} failure(s)`);
  process.exit(1);
}
console.log(`read: all passed — naina ${Module.version()} via onnxruntime-web`);
