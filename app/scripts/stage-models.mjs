// Download the weights the web app serves, into public/models/.
//
// The file list comes from the WASM module's stagingPlan(), which is computed by
// the C++ core from models/registry.yaml. So the registry stays the single
// source of truth for what a tier needs and where it lives — a hardcoded list
// here would drift the first time a model changed.
//
// Why the app serves weights at all: GitHub release assets cannot be fetched
// from a browser. The download 302s to release-assets.githubusercontent.com and
// neither hop sends Access-Control-Allow-Origin. Server-side (here, and in every
// other naina binding) there is no such restriction.
//
// Run:  node scripts/stage-models.mjs [tier...]      default: tiny small
//
// The medium tier is deliberately not offered by the web app: ppdoclayout_l.onnx
// is 129 MB and GitHub Pages caps a single file at 100 MB.

import { mkdir, writeFile, stat } from 'node:fs/promises';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = resolve(HERE, '..', 'public', 'models');
const TIERS = { tiny: 1, small: 2, medium: 3 };

const wanted = process.argv.slice(2).length ? process.argv.slice(2) : ['tiny', 'small'];

// Every alphabet the app offers. Recognition differs per script; detection and
// layout are shared across all of them, and the dedupe below collapses those
// duplicates so they are fetched once rather than once per language.
const LANGS = [
  '',           // Latin, Chinese, Japanese
  'arabic',
  'cyrillic',
  'devanagari',
  'el',
  'eslav',
  'korean',
  'ta',
  'te',
  'th',
];
for (const t of wanted) {
  if (!(t in TIERS)) {
    console.error(`unknown tier '${t}' (expected: ${Object.keys(TIERS).join(', ')})`);
    process.exit(2);
  }
}

const modulePath = resolve(HERE, '..', '..', 'bindings', 'wasm', 'dist', 'naina.mjs');
try {
  await stat(modulePath);
} catch {
  console.error(`${modulePath} is missing — build the WASM binding first.`);
  process.exit(1);
}

const createNaina = (await import(modulePath)).default;
const Module = await createNaina();

await mkdir(OUT, { recursive: true });

// Dedupe across tiers: the layout model for one tier can be shared, and the
// charset files repeat.
const files = new Map();
for (const tier of wanted) {
  for (const lang of LANGS) {
    const plan = JSON.parse(Module.stagingPlan(TIERS[tier], lang));
    if (plan.length === 0) {
      // A language may not exist at every tier; that is not fatal, because
      // resolve() falls back across tiers for the same alphabet.
      console.log(`  (no models for tier '${tier}' lang '${lang || 'default'}')`);
      continue;
    }
    for (const f of plan) {
      files.set(f.url.slice(f.url.lastIndexOf('/') + 1), f);
    }
  }
}
if (files.size === 0) {
  console.error('registry lists no models for the requested tiers');
  process.exit(1);
}

console.log(`staging ${files.size} file(s) for tier(s) ${wanted.join(', ')} across ${LANGS.length} alphabet(s)`);

let total = 0;
for (const [name, f] of files) {
  const dest = join(OUT, name);

  // Skip a file already present at the expected size. The bytes are immutable
  // (the release tag is pinned), so this is safe and makes reruns cheap.
  try {
    const s = await stat(dest);
    if (f.bytes > 0 && s.size === f.bytes) {
      console.log(`  = ${name} (${(s.size / 1e6).toFixed(1)} MB, already staged)`);
      total += s.size;
      continue;
    }
  } catch {
    // not present
  }

  const res = await fetch(f.url, { redirect: 'follow' });
  if (!res.ok) {
    console.error(`  ! ${name}: HTTP ${res.status} from ${f.url}`);
    process.exit(1);
  }
  const buf = Buffer.from(await res.arrayBuffer());

  // The registry records each file's size. A mismatch means the release was
  // changed underneath a pinned tag, which should stop the build rather than
  // ship weights the core will then reject on its sha256 check.
  if (f.bytes > 0 && buf.byteLength !== f.bytes) {
    console.error(`  ! ${name}: got ${buf.byteLength} bytes, registry says ${f.bytes}`);
    process.exit(1);
  }

  await writeFile(dest, buf);
  total += buf.byteLength;
  console.log(`  + ${name} (${(buf.byteLength / 1e6).toFixed(1)} MB)`);
}

console.log(`staged ${(total / 1e6).toFixed(1)} MB into public/models/`);
