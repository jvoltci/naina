# Achroma + naina Adoption Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `achroma`, a zero-dependency achromatic design system, and make naina's web app its first consumer — light-canonical, Geist, film grain, hairline structure — with every existing feature intact.

**Architecture:** Plain CSS custom properties in a new private repo published to npm as `achroma`. The neutral ramp is absolute and machine-checked to be chroma-zero; ~17 colour aliases re-point for dark mode. naina imports the file ahead of its own stylesheet; `main.ts`, `pages.ts` and `ocr.worker.ts` are never touched, so the OCR path cannot regress.

**Tech Stack:** CSS custom properties (OKLCH), Node 22 for tests (no test framework — `node:assert`), Geist Variable / Geist Mono Variable (OFL-1.1), Vite 6 for naina's app, Playwright for its e2e.

**Spec:** [`docs/design/specs/2026-07-30-achroma-design-system-design.md`](../specs/2026-07-30-achroma-design-system-design.md)

---

## Two repos

This plan spans two working directories. **Every task states which.**

| Path | Repo | State |
|---|---|---|
| `~/Documents/code/achroma` | `jvoltci/achroma` | does not exist yet; created in Task 1 |
| `~/Documents/code/naina` | `jvoltci/naina` | on branch `achroma-design-system` |

**Ordering constraint, non-negotiable:** naina's CI runs `actions/checkout@v4` then `npm install` in `app/` and checks out *only naina*. A `file:../../achroma` dependency resolves on a laptop and fails in Actions. So **Task 11 publishes `achroma@0.1.0` before Phase B begins.**

If you would rather not publish yet, Phase B can run against `npm install ../../achroma` — but that rewrites `app/package.json` to a `file:` path which **must not be committed**. Publishing first is the clean path.

## File structure

### New: `~/Documents/code/achroma`

| File | Responsibility |
|---|---|
| `package.json` | name `achroma`, `"dependencies": {}`, `exports` map, `files` allowlist |
| `achroma.css` | the entire system: ramp, aliases, semantics, type, space, radius, motion, `@font-face`, base layer, `.grain`, `.label` |
| `achroma.tailwind.css` | `@theme inline` bridge mapping shadcn's variable names onto Achroma. Written now, unused until cycle 3 |
| `fonts/*.woff2` | 6 vendored subsets |
| `proof.html` | every token rendered; the only way to review a token file |
| `test/oklch.mjs` | OKLCH → linear sRGB → WCAG luminance. Pure functions, no I/O |
| `test/oklch.test.mjs` | pins that math to the sRGB primaries |
| `test/contrast.mjs` | parses `achroma.css`; asserts chroma-zero, monotonicity, block parity, contrast table |
| `.github/workflows/deploy-web.yml` | private repo → public Pages, serving `proof.html` as index |
| `README.md`, `LICENSE`, `NOTICE` | usage; Apache-2.0 to match the org; OFL-1.1 font attribution |

`test/oklch.mjs` is split from `test/contrast.mjs` deliberately. The conversion math must itself be tested — a mistyped coefficient would not throw, it would return plausible contrast ratios that are wrong, and every ratio downstream would inherit the error. That is precisely the failure shape `CLAUDE.md` catalogues.

### Modified: `~/Documents/code/naina`

| File | Change |
|---|---|
| `app/package.json` | add `"achroma": "^0.1.0"` |
| `app/index.html` | rewritten. Every id preserved; Google Fonts link removed; grain element added |
| `app/src/styles.css` | rewritten against tokens |

**Not touched:** `app/src/main.ts`, `app/src/pages.ts`, `app/src/ocr.worker.ts`, `app/public/sw.js`, every test.

### The DOM contract

`main.ts` resolves these by `getElementById`; the Playwright suites query them too. Renaming any is a defect:

```
#drop #file #tier #language #show-boxes #status #status-text #bar-fill
#pager #results #canvas #output #meta #error #offline-badge
#tab-md #tab-txt #tab-json #copy #download #download-all
.page-chip        ← a CLASS. e2e.mjs queries '#pager .page-chip'
```

`.page-chip` is the quiet one: every id failure throws at runtime in `main.ts`, but a `.page-chip` rename only fails in Playwright. Task 17 mutation-checks it.

---

# Phase A — the Achroma repo

## Task 1: Scaffold the repo

**Files:**
- Create: `~/Documents/code/achroma/package.json`
- Create: `~/Documents/code/achroma/.gitignore`
- Create: `~/Documents/code/achroma/README.md`
- Create: `~/Documents/code/achroma/NOTICE`

- [ ] **Step 1: Create the directory and initialise git**

```bash
mkdir -p ~/Documents/code/achroma/{fonts,test,.github/workflows}
cd ~/Documents/code/achroma
git init
```

Expected: `Initialized empty Git repository in .../achroma/.git/`

- [ ] **Step 2: Write `package.json`**

```json
{
  "name": "achroma",
  "version": "0.1.0",
  "description": "An achromatic design system. Black, white and greys, with hue reserved for meaning. Zero dependencies, plain CSS custom properties.",
  "license": "Apache-2.0",
  "type": "module",
  "keywords": ["design-system", "design-tokens", "css", "achromatic", "monochrome", "greyscale"],
  "repository": {
    "type": "git",
    "url": "git+https://github.com/jvoltci/achroma.git"
  },
  "homepage": "https://jvoltci.github.io/achroma/",
  "exports": {
    "./achroma.css": "./achroma.css",
    "./achroma.tailwind.css": "./achroma.tailwind.css",
    "./fonts/*": "./fonts/*"
  },
  "files": ["achroma.css", "achroma.tailwind.css", "fonts", "NOTICE"],
  "sideEffects": ["*.css"],
  "dependencies": {},
  "scripts": {
    "test": "node test/oklch.test.mjs && node test/contrast.mjs"
  }
}
```

`"dependencies": {}` is written explicitly rather than omitted — it is the promise the package makes, and an empty object states it where a missing key merely implies it.

- [ ] **Step 3: Write `.gitignore`**

```
node_modules/
.DS_Store
_site/
```

- [ ] **Step 4: Write `NOTICE`**

```
achroma
Copyright 2026 jvoltci

Licensed under the Apache License, Version 2.0.

── Bundled fonts ────────────────────────────────────────────────────────

fonts/geist-*.woff2
fonts/geist-mono-*.woff2

  Geist and Geist Mono
  Copyright 2024 The Geist Project Authors
  https://github.com/vercel/geist-font

  Licensed under the SIL Open Font License, Version 1.1.
  http://scripts.sil.org/OFL

  Subsets extracted from @fontsource-variable/geist@5.3.0 and
  @fontsource-variable/geist-mono@5.3.0.
```

- [ ] **Step 5: Write `README.md`**

````markdown
# achroma

An achromatic design system. Black, white and greys, with hue reserved for
meaning.

Zero dependencies. Plain CSS custom properties, no build step, no runtime. React,
Next, Vite, Astro and a bare `.html` file all consume the identical file.

Live token reference: <https://jvoltci.github.io/achroma/>

## Install

```bash
npm i achroma
```

```css
@import 'achroma/achroma.css';
```

No tooling? `<link rel="stylesheet" href="https://unpkg.com/achroma/achroma.css">`

Tailwind v4 + shadcn, additionally:

```css
@import 'achroma/achroma.tailwind.css';
```

## The two rules

1. **The ramp is absolute.** Every `--n-*` is chroma `0`, hue `0`, and identical
   in both modes. `npm test` fails if that stops being true.
2. **Aliases are what flip.** Dark mode re-points ~17 aliases at different ramp
   steps. It is not a second palette to keep in sync.

## Colour is never decoration

Hue appears only in `--danger-*`, `--warn-*` and `--ok-*`, and it never governs
content. Anything a user *chose* or a document *contains* is outside this system's
reach — a yellow highlighter has to be yellow, and a confidence heat-map has to
run green to amber.

Each semantic has three tokens because one cannot do three jobs: `-text` clears
4.5:1 on the page background, `-line` clears 3:1 for borders, `-bg` is a subtle
fill. Amber is why — `oklch(0.62 0.13 75)` on paper is 3.57:1, so an amber bright
enough to read as a warning border can never also be legible body text.

## Dark mode

Set nothing and it follows `prefers-color-scheme`. Class-based toggling (e.g.
next-themes) works too: `.dark` or `[data-theme='dark']`.

## Tokens

See `achroma.css` — it is the documentation. `proof.html` renders all of it.
````

- [ ] **Step 6: Add Apache-2.0 `LICENSE`**

```bash
cd ~/Documents/code/achroma
curl -sSL https://www.apache.org/licenses/LICENSE-2.0.txt -o LICENSE
head -3 LICENSE
```

Expected: `Apache License` / `Version 2.0, January 2004` / `http://www.apache.org/licenses/`

- [ ] **Step 7: Commit**

```bash
cd ~/Documents/code/achroma
git add -A
git commit -m "chore: scaffold achroma, a zero-dependency achromatic design system"
```

---

## Task 2: The OKLCH math, test-first

**Files:**
- Create: `~/Documents/code/achroma/test/oklch.mjs`
- Test: `~/Documents/code/achroma/test/oklch.test.mjs`

Everything downstream trusts these numbers, so they get pinned to known values first.

- [ ] **Step 1: Write the failing test**

`test/oklch.test.mjs`:

```js
// Pins the OKLCH conversion to values that are not opinions.
//
// The sRGB primaries have exact OKLCH coordinates and exact WCAG luminances, so
// a wrong matrix coefficient shows up here rather than as a contrast ratio that
// looks reasonable and is wrong.

import assert from 'node:assert/strict';
import { parseOklch, oklchToLinearSrgb, luminance, contrast } from './oklch.mjs';

let passed = 0;
const test = (name, fn) => {
  fn();
  passed++;
  console.log(`  ok  ${name}`);
};
const close = (actual, expected, tol, what) =>
  assert.ok(
    Math.abs(actual - expected) <= tol,
    `${what}: expected ${expected} ± ${tol}, got ${actual}`,
  );

test('parseOklch reads all three components', () => {
  assert.deepEqual(parseOklch('oklch(0.985 0 0)'), { L: 0.985, C: 0, h: 0 });
  assert.deepEqual(parseOklch('  oklch(0.5 0.19 27)  '), { L: 0.5, C: 0.19, h: 27 });
});

test('parseOklch returns null for anything else', () => {
  assert.equal(parseOklch('var(--n-25)'), null);
  assert.equal(parseOklch('#fafafa'), null);
  assert.equal(parseOklch('oklch(0.5 0.19)'), null);
});

test('white is luminance 1, black is luminance 0', () => {
  close(luminance(oklchToLinearSrgb({ L: 1, C: 0, h: 0 })), 1, 0.002, 'white');
  close(luminance(oklchToLinearSrgb({ L: 0, C: 0, h: 0 })), 0, 0.002, 'black');
});

test('the sRGB red primary round-trips', () => {
  const { r, g, b } = oklchToLinearSrgb({ L: 0.62796, C: 0.25768, h: 29.234 });
  close(r, 1, 0.01, 'red.r');
  close(g, 0, 0.01, 'red.g');
  close(b, 0, 0.01, 'red.b');
  close(luminance({ r, g, b }), 0.2126, 0.005, 'red luminance');
});

test('the sRGB green primary round-trips', () => {
  const { r, g, b } = oklchToLinearSrgb({ L: 0.86644, C: 0.29483, h: 142.495 });
  close(r, 0, 0.01, 'green.r');
  close(g, 1, 0.01, 'green.g');
  close(b, 0, 0.01, 'green.b');
  close(luminance({ r, g, b }), 0.7152, 0.005, 'green luminance');
});

test('the sRGB blue primary round-trips', () => {
  const { r, g, b } = oklchToLinearSrgb({ L: 0.45201, C: 0.31321, h: 264.052 });
  close(r, 0, 0.01, 'blue.r');
  close(g, 0, 0.01, 'blue.g');
  close(b, 1, 0.01, 'blue.b');
  close(luminance({ r, g, b }), 0.0722, 0.005, 'blue luminance');
});

test('black on white is 21:1', () => {
  close(contrast({ L: 0, C: 0, h: 0 }, { L: 1, C: 0, h: 0 }), 21, 0.05, 'max contrast');
});

test('contrast is symmetric', () => {
  const a = { L: 0.13, C: 0, h: 0 };
  const b = { L: 0.985, C: 0, h: 0 };
  assert.equal(contrast(a, b), contrast(b, a));
});

// The whole ramp relies on this identity: with chroma 0 the OKLab a and b terms
// vanish, l_ = m_ = s_ = L, and the linear-sRGB matrix rows sum to 1 — so
// luminance is exactly L cubed. It is what makes the ramp checkable by hand.
test('an achromatic colour has luminance L cubed', () => {
  for (const L of [0.09, 0.17, 0.52, 0.84, 0.922, 0.985]) {
    close(luminance(oklchToLinearSrgb({ L, C: 0, h: 0 })), L ** 3, 0.0005, `L=${L}`);
  }
});

console.log(`\n${passed} passed`);
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cd ~/Documents/code/achroma
node test/oklch.test.mjs
```

Expected: `Error [ERR_MODULE_NOT_FOUND]: Cannot find module '.../test/oklch.mjs'`

- [ ] **Step 3: Write the implementation**

`test/oklch.mjs`:

```js
// OKLCH -> linear sRGB -> WCAG relative luminance.
//
// Hand-written because the alternative is a dependency in a package that
// promises none, and because these numbers decide whether the ramp ships. A
// wrong coefficient would not throw — it would return plausible contrast ratios
// that are wrong. oklch.test.mjs pins it to the sRGB primaries.
//
// Matrices: Bjorn Ottosson's OKLab, https://bottosson.github.io/posts/oklab/

/** @typedef {{ L: number, C: number, h: number }} Oklch */

/**
 * Parse `oklch(L C H)` with unitless components.
 * @returns {Oklch | null} null when the value is not a plain oklch() literal.
 */
export function parseOklch(value) {
  const m = /^oklch\(\s*([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)\s*\)$/.exec(String(value).trim());
  if (!m) return null;
  return { L: Number(m[1]), C: Number(m[2]), h: Number(m[3]) };
}

/**
 * OKLCH to linear-light sRGB. Deliberately unclamped: a channel outside [0,1]
 * means the colour is outside the sRGB gamut, and callers may want to know.
 * @param {Oklch} colour
 */
export function oklchToLinearSrgb({ L, C, h }) {
  const hr = (h * Math.PI) / 180;
  const a = C * Math.cos(hr);
  const b = C * Math.sin(hr);

  const l_ = L + 0.3963377774 * a + 0.2158037573 * b;
  const m_ = L - 0.1055613458 * a - 0.0638541728 * b;
  const s_ = L - 0.0894841775 * a - 1.2914855480 * b;

  const l = l_ * l_ * l_;
  const m = m_ * m_ * m_;
  const s = s_ * s_ * s_;

  return {
    r: 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
    g: -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
    b: -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
  };
}

/**
 * WCAG 2.x relative luminance. The input is already linear-light, which is what
 * WCAG's gamma-decode step produces, so there is no further decoding to do.
 * Out-of-gamut channels are clamped, because a display cannot show them.
 */
export function luminance({ r, g, b }) {
  const c = (v) => Math.min(1, Math.max(0, v));
  return 0.2126 * c(r) + 0.7152 * c(g) + 0.0722 * c(b);
}

/**
 * WCAG 2.x contrast ratio, 1..21.
 * @param {Oklch} a
 * @param {Oklch} b
 */
export function contrast(a, b) {
  const ya = luminance(oklchToLinearSrgb(a));
  const yb = luminance(oklchToLinearSrgb(b));
  const [hi, lo] = ya >= yb ? [ya, yb] : [yb, ya];
  return (hi + 0.05) / (lo + 0.05);
}
```

- [ ] **Step 4: Run it to confirm it passes**

```bash
cd ~/Documents/code/achroma
node test/oklch.test.mjs
```

Expected: nine `ok` lines then `9 passed`. If a primary is off by more than the tolerance, a matrix coefficient is mistyped — fix the matrix, never the tolerance.

- [ ] **Step 5: Commit**

```bash
cd ~/Documents/code/achroma
git add test/oklch.mjs test/oklch.test.mjs
git commit -m "test: pin OKLCH-to-luminance conversion to the sRGB primaries"
```

---

## Task 3: The ramp assertions, test-first

**Files:**
- Create: `~/Documents/code/achroma/test/contrast.mjs`

This runs before `achroma.css` exists, so it fails for the right reason first.

- [ ] **Step 1: Write `test/contrast.mjs`**

```js
// Asserts the things about achroma.css that cannot be seen by reading it.
//
// Four classes of check, in order of how quietly they would otherwise fail:
//
//   1. chroma-zero    a stray 0.01 in the ramp is invisible by eye and would
//                     propagate to every site that installs this
//   2. block parity   the dark values are written twice (media query + class)
//                     and drift between the copies is undetectable by hand
//   3. monotonicity   a ramp step out of order makes "one step darker" a lie
//   4. contrast       computed, never assumed
//
// Prints every ratio whether it passes or not. A pass/fail line tells you less
// than the numbers do.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { parseOklch, contrast } from './oklch.mjs';

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const css = readFileSync(join(root, 'achroma.css'), 'utf8');

const failures = [];
const check = (ok, message) => {
  if (!ok) failures.push(message);
  return ok;
};

/** The 17 aliases that must be defined in both modes. This list is the contract. */
const COLOUR_ALIASES = [
  '--bg', '--bg-raised', '--bg-sunken',
  '--fg', '--fg-dim', '--fg-faint',
  '--hairline', '--rule',
  '--danger-text', '--danger-line', '--danger-bg',
  '--warn-text', '--warn-line', '--warn-bg',
  '--ok-text', '--ok-line', '--ok-bg',
];

/** `[alias, against, minimum]` — the reason each token exists, as a number. */
const TARGETS = [
  ['--fg', '--bg', 7.0],
  ['--fg-dim', '--bg', 4.5],
  ['--fg-faint', '--bg', 3.0],
  ['--rule', '--bg', 1.4],
  ['--hairline', '--bg', 1.15],
  ['--danger-text', '--bg', 4.5],
  ['--warn-text', '--bg', 4.5],
  ['--ok-text', '--bg', 4.5],
  ['--danger-line', '--bg', 3.0],
  ['--warn-line', '--bg', 3.0],
  ['--ok-line', '--bg', 3.0],
  ['--danger-text', '--danger-bg', 4.5],
  ['--warn-text', '--warn-bg', 4.5],
  ['--ok-text', '--ok-bg', 4.5],
];

/** Split the file on `/* @achroma <name> *​/` markers. */
function blocks(source) {
  const marks = [...source.matchAll(/\/\*\s*@achroma\s+([a-z-]+)\s*\*\//g)];
  check(
    marks.length === 3,
    `expected 3 @achroma markers (light, dark, dark-class), found ${marks.length}`,
  );
  const out = new Map();
  marks.forEach((mark, i) => {
    const start = mark.index + mark[0].length;
    const end = i + 1 < marks.length ? marks[i + 1].index : source.length;
    out.set(mark[1], source.slice(start, end));
  });
  return out;
}

/** Every `--token: value;` in a chunk, last definition winning. */
function decls(chunk) {
  const out = new Map();
  for (const m of chunk.matchAll(/(--[a-z0-9-]+)\s*:\s*([^;]+);/g)) {
    out.set(m[1], m[2].trim());
  }
  return out;
}

/** Resolve a declaration to OKLCH, following one level of var() into the ramp. */
function resolve(value, ramp) {
  const direct = parseOklch(value);
  if (direct) return direct;
  const v = /^var\(\s*(--[a-z0-9-]+)\s*\)$/.exec(value);
  return v && ramp.has(v[1]) ? ramp.get(v[1]) : null;
}

const parts = blocks(css);
const light = decls(parts.get('light') ?? '');
const dark = decls(parts.get('dark') ?? '');
const darkClass = decls(parts.get('dark-class') ?? '');

// ── 1. the ramp is achromatic ─────────────────────────────────────────
const ramp = new Map();
for (const [name, value] of light) {
  if (!/^--n-\d+$/.test(name)) continue;
  const c = parseOklch(value);
  if (!check(c !== null, `${name}: not a plain oklch() literal — got ${value}`)) continue;
  check(c.C === 0, `${name}: chroma must be exactly 0, got ${c.C} — this is not achromatic`);
  check(c.h === 0, `${name}: hue must be exactly 0, got ${c.h}`);
  ramp.set(name, c);
}
check(ramp.size === 16, `expected 16 ramp steps, found ${ramp.size}`);

// ── 2. the ramp is ordered ────────────────────────────────────────────
const ordered = [...ramp.entries()].sort(
  (a, b) => Number(a[0].slice(4)) - Number(b[0].slice(4)),
);
for (let i = 1; i < ordered.length; i++) {
  const [prevName, prev] = ordered[i - 1];
  const [name, cur] = ordered[i];
  check(
    cur.L < prev.L,
    `ramp out of order: ${name} (L=${cur.L}) is not darker than ${prevName} (L=${prev.L})`,
  );
}

// ── 3. the two dark blocks agree ──────────────────────────────────────
// They are written twice on purpose — a media query for OS preference, a class
// for next-themes. Nothing but this check would catch them diverging.
for (const alias of COLOUR_ALIASES) {
  check(dark.has(alias), `--dark block is missing ${alias}`);
  check(darkClass.has(alias), `--dark-class block is missing ${alias}`);
  if (dark.has(alias) && darkClass.has(alias)) {
    check(
      dark.get(alias) === darkClass.get(alias),
      `dark blocks disagree on ${alias}: media says ${dark.get(alias)}, class says ${darkClass.get(alias)}`,
    );
  }
  check(light.has(alias), `light block is missing ${alias}`);
}

// ── 4. contrast ───────────────────────────────────────────────────────
for (const [mode, table] of [['light', light], ['dark', new Map([...light, ...dark])]]) {
  console.log(`\n${mode}`);
  for (const [fg, bg, min] of TARGETS) {
    const a = resolve(table.get(fg) ?? '', ramp);
    const b = resolve(table.get(bg) ?? '', ramp);
    if (!check(a && b, `${mode}: cannot resolve ${fg} on ${bg}`)) continue;
    const ratio = contrast(a, b);
    const ok = ratio >= min;
    check(ok, `${mode}: ${fg} on ${bg} is ${ratio.toFixed(2)}:1, need >= ${min}:1`);
    console.log(
      `  ${ok ? 'ok  ' : 'FAIL'} ${fg} on ${bg}  ${ratio.toFixed(2)}:1  (>= ${min})`,
    );
  }
}

if (failures.length) {
  console.error(`\n${failures.length} failure(s):`);
  for (const f of failures) console.error(`  - ${f}`);
  process.exit(1);
}
console.log('\nall ramp assertions passed');
```

- [ ] **Step 2: Run it to confirm it fails for the right reason**

```bash
cd ~/Documents/code/achroma
node test/contrast.mjs
```

Expected: `Error: ENOENT: no such file or directory, open '.../achroma.css'`

That is the correct failure. If it prints contrast numbers, something is wrong.

- [ ] **Step 3: Commit**

```bash
cd ~/Documents/code/achroma
git add test/contrast.mjs
git commit -m "test: assert chroma-zero, ramp order, dark-block parity and contrast"
```

---

## Task 4: Vendor the fonts

**Files:**
- Create: `~/Documents/code/achroma/fonts/` (6 `.woff2`)

- [ ] **Step 1: Extract the six subsets**

```bash
cd /tmp && rm -rf achroma-fonts && mkdir achroma-fonts && cd achroma-fonts
npm pack @fontsource-variable/geist@5.3.0 @fontsource-variable/geist-mono@5.3.0
for t in *.tgz; do tar xzf "$t"; mv package "${t%.tgz}"; done
cp fontsource-variable-geist-5.3.0/files/geist-latin-wght-normal.woff2 \
   fontsource-variable-geist-5.3.0/files/geist-latin-ext-wght-normal.woff2 \
   fontsource-variable-geist-5.3.0/files/geist-cyrillic-wght-normal.woff2 \
   fontsource-variable-geist-mono-5.3.0/files/geist-mono-latin-wght-normal.woff2 \
   fontsource-variable-geist-mono-5.3.0/files/geist-mono-latin-ext-wght-normal.woff2 \
   fontsource-variable-geist-mono-5.3.0/files/geist-mono-cyrillic-wght-normal.woff2 \
   ~/Documents/code/achroma/fonts/
ls -la ~/Documents/code/achroma/fonts/
```

Expected: 6 files. Italics are skipped — an editorial achromatic system leans on weight, not slant.

- [ ] **Step 2: Confirm the licence is what NOTICE claims**

```bash
grep -c "SIL Open Font License" /tmp/achroma-fonts/fontsource-variable-geist-5.3.0/LICENSE
```

Expected: a count `>= 1`. This was verified as OFL-1.1 on 2026-07-30; re-checking costs a second and the `NOTICE` file asserts it.

- [ ] **Step 3: Commit**

```bash
cd ~/Documents/code/achroma
git add fonts/
git commit -m "chore: vendor Geist and Geist Mono subsets (OFL-1.1)"
```

---

## Task 5: `achroma.css`

**Files:**
- Create: `~/Documents/code/achroma/achroma.css`

- [ ] **Step 1: Write the file**

```css
/* Achroma — an achromatic design system.
 *
 * Black, white and greys, with hue reserved for meaning. Plain CSS custom
 * properties: no build step, no runtime, zero dependencies.
 *
 * Two rules hold this together:
 *
 *   1. The ramp is absolute. Every --n-* is chroma 0, hue 0, identical in both
 *      modes. test/contrast.mjs fails if that stops being true, because a stray
 *      0.01 is invisible by eye and would reach every site that installs this.
 *
 *   2. Aliases are what flip. Dark mode re-points 17 aliases at different ramp
 *      steps — it is not a second palette to keep in sync.
 *
 * Colour is never decoration. It appears only in --danger/--warn/--ok, and it
 * never governs content a user chose or a document contains.
 *
 * The @achroma marker comments are parsed by test/contrast.mjs. Do not remove
 * them.
 */

/* ── fonts ────────────────────────────────────────────────────────────
 *
 * Self-hosted, not Google Fonts: consumers of this system claim to work
 * offline, and a CDN webfont would break that claim quietly.
 *
 * Family names are exact. 'Geist Variable' and 'Geist Mono Variable' — the word
 * order on the mono is easy to reverse, and getting it wrong falls back to the
 * system monospace silently.
 *
 * Geist covers latin, latin-ext and cyrillic. It does NOT cover Devanagari,
 * Arabic, Greek, Korean, Tamil, Telugu or Thai; the fallback chain on
 * --font-mono is what handles those, per-glyph.
 */

@font-face {
  font-family: 'Geist Variable';
  font-style: normal;
  font-display: swap;
  font-weight: 100 900;
  src: url('./fonts/geist-latin-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0000-00FF, U+0131, U+0152-0153, U+02BB-02BC, U+02C6, U+02DA,
    U+02DC, U+0304, U+0308, U+0329, U+2000-206F, U+20AC, U+2122, U+2191, U+2193,
    U+2212, U+2215, U+FEFF, U+FFFD;
}

@font-face {
  font-family: 'Geist Variable';
  font-style: normal;
  font-display: swap;
  font-weight: 100 900;
  src: url('./fonts/geist-latin-ext-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0100-02BA, U+02BD-02C5, U+02C7-02CC, U+02CE-02D7, U+02DD-02FF,
    U+0304, U+0308, U+0329, U+1D00-1DBF, U+1E00-1E9F, U+1EF2-1EFF, U+2020,
    U+20A0-20AB, U+20AD-20C0, U+2113, U+2C60-2C7F, U+A720-A7FF;
}

@font-face {
  font-family: 'Geist Variable';
  font-style: normal;
  font-display: swap;
  font-weight: 100 900;
  src: url('./fonts/geist-cyrillic-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0301, U+0400-045F, U+0490-0491, U+04B0-04B1, U+2116;
}

@font-face {
  font-family: 'Geist Mono Variable';
  font-style: normal;
  font-display: swap;
  font-weight: 100 900;
  src: url('./fonts/geist-mono-latin-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0000-00FF, U+0131, U+0152-0153, U+02BB-02BC, U+02C6, U+02DA,
    U+02DC, U+0304, U+0308, U+0329, U+2000-206F, U+20AC, U+2122, U+2191, U+2193,
    U+2212, U+2215, U+FEFF, U+FFFD;
}

@font-face {
  font-family: 'Geist Mono Variable';
  font-style: normal;
  font-display: swap;
  font-weight: 100 900;
  src: url('./fonts/geist-mono-latin-ext-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0100-02BA, U+02BD-02C5, U+02C7-02CC, U+02CE-02D7, U+02DD-02FF,
    U+0304, U+0308, U+0329, U+1D00-1DBF, U+1E00-1E9F, U+1EF2-1EFF, U+2020,
    U+20A0-20AB, U+20AD-20C0, U+2113, U+2C60-2C7F, U+A720-A7FF;
}

@font-face {
  font-family: 'Geist Mono Variable';
  font-style: normal;
  font-display: swap;
  font-weight: 100 900;
  src: url('./fonts/geist-mono-cyrillic-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0301, U+0400-045F, U+0490-0491, U+04B0-04B1, U+2116;
}

/* @achroma light */
:root {
  color-scheme: light dark;

  /* ── the ramp — absolute, achromatic, mode-independent ─────────────
   *
   * Chroma and hue are exactly 0, which collapses the OKLCH-to-luminance
   * chain to Y = L cubed for the whole ladder. That is why these values can
   * be reasoned about by hand.
   */
  --n-0: oklch(1.000 0 0);
  --n-25: oklch(0.985 0 0);
  --n-50: oklch(0.968 0 0);
  --n-100: oklch(0.945 0 0);
  --n-150: oklch(0.922 0 0);
  --n-200: oklch(0.900 0 0);
  --n-300: oklch(0.840 0 0);
  --n-400: oklch(0.720 0 0);
  --n-500: oklch(0.620 0 0);
  --n-600: oklch(0.520 0 0);
  --n-700: oklch(0.400 0 0);
  --n-800: oklch(0.300 0 0);
  --n-850: oklch(0.220 0 0);
  --n-900: oklch(0.170 0 0);
  --n-950: oklch(0.130 0 0);
  --n-1000: oklch(0.090 0 0);

  /* ── aliases — light ──────────────────────────────────────────────── */
  --bg: var(--n-25);
  --bg-raised: var(--n-0);
  --bg-sunken: var(--n-50);
  --fg: var(--n-950);
  --fg-dim: var(--n-600);
  --fg-faint: var(--n-500);
  --hairline: var(--n-150);
  --rule: var(--n-300);

  /* ── semantics — the only colour in the system ─────────────────────
   *
   * Three tokens each, because one cannot do three jobs. -text clears 4.5:1
   * on --bg, -line clears 3:1, -bg is a subtle fill. Amber is the proof:
   * oklch(0.62 0.13 75) on paper is 3.57:1, so an amber bright enough to
   * read as a border can never also be legible body text.
   */
  --danger-text: oklch(0.50 0.19 27);
  --danger-line: oklch(0.62 0.17 27);
  --danger-bg: oklch(0.965 0.015 27);
  --warn-text: oklch(0.50 0.11 75);
  --warn-line: oklch(0.66 0.13 78);
  --warn-bg: oklch(0.968 0.022 85);
  --ok-text: oklch(0.48 0.12 150);
  --ok-line: oklch(0.58 0.12 150);
  --ok-bg: oklch(0.965 0.018 150);

  /* ── type ─────────────────────────────────────────────────────────── */
  --font-sans: 'Geist Variable', ui-sans-serif, system-ui, -apple-system, sans-serif;
  --font-mono: 'Geist Mono Variable', ui-monospace, 'SF Mono', Menlo, monospace;

  --text-2xs: 0.6875rem;
  --text-xs: 0.75rem;
  --text-sm: 0.8125rem;
  --text-base: 0.9375rem;
  --text-md: 1.0625rem;
  --text-lg: 1.25rem;
  --text-xl: 1.5rem;
  --text-2xl: 1.875rem;
  --text-display: clamp(2.5rem, 6vw, 5rem);

  --w-thin: 200;
  --w-light: 300;
  --w-regular: 400;
  --w-medium: 500;

  /* Huge-and-thin against tiny-and-wide. This tension is the signature. */
  --track-display: -0.035em;
  --track-tight: -0.015em;
  --track-normal: 0;
  --track-label: 0.14em;

  --lh-display: 1.02;
  --lh-tight: 1.25;
  --lh-body: 1.6;

  /* ── space — 4px base ─────────────────────────────────────────────── */
  --s-1: 0.25rem;
  --s-2: 0.5rem;
  --s-3: 0.75rem;
  --s-4: 1rem;
  --s-5: 1.25rem;
  --s-6: 1.5rem;
  --s-8: 2rem;
  --s-10: 2.5rem;
  --s-12: 3rem;
  --s-16: 4rem;
  --s-20: 5rem;
  --s-24: 6rem;

  /* ── radius — near-sharp on purpose ───────────────────────────────── */
  --r-0: 0;
  --r-sm: 2px;
  --r-md: 4px;
  --r-lg: 8px;

  /* ── motion ───────────────────────────────────────────────────────── */
  --ease-spring: cubic-bezier(0.16, 1, 0.3, 1);
  --ease-out: cubic-bezier(0.33, 1, 0.68, 1);
  --dur-1: 120ms;
  --dur-2: 180ms;
  --dur-3: 280ms;
  --dur-4: 420ms;
  --stagger: 40ms;

  /* ── texture ──────────────────────────────────────────────────────── */
  --grain-opacity: 0.025;
}

/* @achroma dark */
@media (prefers-color-scheme: dark) {
  :root:not([data-theme='light']):not(.light) {
    --bg: var(--n-900);
    --bg-raised: var(--n-850);
    --bg-sunken: var(--n-950);
    --fg: var(--n-50);
    --fg-dim: var(--n-400);
    --fg-faint: var(--n-500);
    --hairline: var(--n-800);
    --rule: var(--n-700);
    --danger-text: oklch(0.72 0.16 25);
    --danger-line: oklch(0.50 0.15 27);
    --danger-bg: oklch(0.240 0.045 27);
    --warn-text: oklch(0.82 0.13 82);
    --warn-line: oklch(0.55 0.11 78);
    --warn-bg: oklch(0.240 0.040 80);
    --ok-text: oklch(0.78 0.13 155);
    --ok-line: oklch(0.52 0.11 152);
    --ok-bg: oklch(0.230 0.040 152);
    --grain-opacity: 0.035;
  }
}

/* @achroma dark-class */
/* Written twice on purpose. The media query serves consumers that follow the
 * OS (naina); the class serves consumers with a toggle (next-themes sets
 * .dark). test/contrast.mjs asserts the two blocks are identical, because
 * nothing else would catch them drifting. */
:root[data-theme='dark'],
.dark {
  color-scheme: dark;
  --bg: var(--n-900);
  --bg-raised: var(--n-850);
  --bg-sunken: var(--n-950);
  --fg: var(--n-50);
  --fg-dim: var(--n-400);
  --fg-faint: var(--n-500);
  --hairline: var(--n-800);
  --rule: var(--n-700);
  --danger-text: oklch(0.72 0.16 25);
  --danger-line: oklch(0.50 0.15 27);
  --danger-bg: oklch(0.240 0.045 27);
  --warn-text: oklch(0.82 0.13 82);
  --warn-line: oklch(0.55 0.11 78);
  --warn-bg: oklch(0.240 0.040 80);
  --ok-text: oklch(0.78 0.13 155);
  --ok-line: oklch(0.52 0.11 152);
  --ok-bg: oklch(0.230 0.040 152);
  --grain-opacity: 0.035;
}

/* ── base ─────────────────────────────────────────────────────────────
 *
 * Deliberately small. An opinionated reset in a shared package fights each
 * consumer's own base styles, so this sets only what the token system needs
 * to be true.
 */

*,
*::before,
*::after {
  box-sizing: border-box;
}

html {
  -webkit-text-size-adjust: 100%;
}

body {
  margin: 0;
  background: var(--bg);
  color: var(--fg);
  font-family: var(--font-sans);
  font-size: var(--text-base);
  font-weight: var(--w-light);
  line-height: var(--lh-body);
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
}

:focus-visible {
  outline: 2px solid var(--fg);
  outline-offset: 2px;
}

/* ── the grain overlay ────────────────────────────────────────────────
 *
 * One fixed element, no JavaScript. This is what makes an achromatic surface
 * read as material rather than flat — the single highest-leverage detail in
 * the system.
 *
 * Usage: <div class="grain" aria-hidden="true"></div> as the last child of
 * body.
 */

.grain {
  position: fixed;
  inset: 0;
  z-index: 9999;
  pointer-events: none;
  opacity: var(--grain-opacity);
  background-repeat: repeat;
  background-size: 180px 180px;
  background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='180' height='180'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.8' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='180' height='180' filter='url(%23n)'/%3E%3C/svg%3E");
}

/* ── the micro-label ──────────────────────────────────────────────────
 *
 * The other half of the type signature: tiny, wide, uppercase, mono.
 */

.label {
  font-family: var(--font-mono);
  font-size: var(--text-2xs);
  font-weight: var(--w-medium);
  letter-spacing: var(--track-label);
  text-transform: uppercase;
  color: var(--fg-faint);
}

@media (prefers-reduced-motion: reduce) {
  :root {
    --dur-1: 0ms;
    --dur-2: 0ms;
    --dur-3: 0ms;
    --dur-4: 0ms;
    --stagger: 0ms;
  }

  *,
  *::before,
  *::after {
    animation-duration: 0.01ms !important;
    animation-iteration-count: 1 !important;
    transition-duration: 0.01ms !important;
    scroll-behavior: auto !important;
  }

  .grain {
    display: none;
  }
}
```

- [ ] **Step 2: Run the assertions**

```bash
cd ~/Documents/code/achroma
node test/contrast.mjs
```

Expected: a light table and a dark table of ratios, then `all ramp assertions passed`.

**If anything reports FAIL, retune the token and re-run — never lower the target.**

The `-line` tokens on light are the tight ones, because yellow and green carry a lot of luminance and `3:1` against near-white paper is demanding. `--warn-line` and `--ok-line` were already pulled down from `0.72` and `0.62` to `0.66` and `0.58` for exactly this reason, and they may still be marginal. Drop `L` in 0.02 steps until each clears. `--danger-line` has headroom (red is dark) and should pass at `0.62`.

Retuning `L` is always the right lever; reducing `C` desaturates toward the neutrals and blurs the signal-versus-decoration line the system depends on.

- [ ] **Step 3: Verify the achromatic assertion is load-bearing**

Break it deliberately, confirm the test catches it, put it back:

```bash
cd ~/Documents/code/achroma
sed -i '' 's/--n-500: oklch(0.620 0 0);/--n-500: oklch(0.620 0.01 250);/' achroma.css
node test/contrast.mjs; echo "exit=$?"
```

Expected: `exit=1`, with `--n-500: chroma must be exactly 0, got 0.01 — this is not achromatic`.

```bash
cd ~/Documents/code/achroma
sed -i '' 's/--n-500: oklch(0.620 0.01 250);/--n-500: oklch(0.620 0 0);/' achroma.css
node test/contrast.mjs; echo "exit=$?"
```

Expected: `exit=0`. A test that cannot fail is not evidence.

- [ ] **Step 4: Verify the dark-block parity assertion is load-bearing**

```bash
cd ~/Documents/code/achroma
sed -i '' '/@achroma dark-class/,$ s/--fg-dim: var(--n-400);/--fg-dim: var(--n-500);/' achroma.css
node test/contrast.mjs; echo "exit=$?"
```

Expected: `exit=1`, with `dark blocks disagree on --fg-dim`.

```bash
cd ~/Documents/code/achroma
sed -i '' '/@achroma dark-class/,$ s/--fg-dim: var(--n-500);/--fg-dim: var(--n-400);/' achroma.css
node test/contrast.mjs; echo "exit=$?"
```

Expected: `exit=0`.

- [ ] **Step 5: Commit**

```bash
cd ~/Documents/code/achroma
git add achroma.css
git commit -m "feat: the token system — absolute ramp, mode-flipping aliases, grain"
```

---

## Task 6: The Tailwind bridge

**Files:**
- Create: `~/Documents/code/achroma/achroma.tailwind.css`

Written now, unused until the `tool` cycle. It exists so that cycle is a two-line change rather than a rediscovery.

- [ ] **Step 1: Write the file**

```css
/* Achroma -> Tailwind v4 + shadcn bridge.
 *
 * Import after achroma.css. Maps shadcn's semantic variable names onto Achroma
 * tokens so Radix components inherit the system without any edits to their own
 * files.
 *
 *   @import 'achroma/achroma.css';
 *   @import 'achroma/achroma.tailwind.css';
 *
 * Unused by naina — naina is plain CSS. This is for consumers on Tailwind.
 */

@theme inline {
  --color-background: var(--bg);
  --color-foreground: var(--fg);
  --color-card: var(--bg-raised);
  --color-card-foreground: var(--fg);
  --color-popover: var(--bg-raised);
  --color-popover-foreground: var(--fg);

  /* Interaction is ink, not a hue. This is the line that makes a shadcn app
   * achromatic — --primary is where its accent colour lived. */
  --color-primary: var(--fg);
  --color-primary-foreground: var(--bg);
  --color-secondary: var(--bg-sunken);
  --color-secondary-foreground: var(--fg);
  --color-muted: var(--bg-sunken);
  --color-muted-foreground: var(--fg-dim);
  --color-accent: var(--bg-sunken);
  --color-accent-foreground: var(--fg);

  --color-destructive: var(--danger-text);
  --color-destructive-foreground: var(--n-0);

  --color-border: var(--hairline);
  --color-input: var(--rule);
  --color-ring: var(--fg);

  --font-sans: var(--font-sans);
  --font-mono: var(--font-mono);

  --radius-sm: var(--r-sm);
  --radius-md: var(--r-md);
  --radius-lg: var(--r-lg);
  --radius-xl: var(--r-lg);
}
```

Note what is deliberately absent: `--chart-1` through `--chart-5`. Series in an achromatic system cannot be told apart by hue, so charts need a lightness ramp plus pattern differentiation — a real problem that belongs in the `tool` cycle with the `dataviz` skill, not a blind five-grey substitution here.

- [ ] **Step 2: Commit**

```bash
cd ~/Documents/code/achroma
git add achroma.tailwind.css
git commit -m "feat: Tailwind v4 + shadcn bridge, for the tool cycle"
```

---

## Task 7: `proof.html`

**Files:**
- Create: `~/Documents/code/achroma/proof.html`

A token file cannot be reviewed by reading it. This is the review surface.

- [ ] **Step 1: Write the file**

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>achroma — token reference</title>
    <link rel="stylesheet" href="./achroma.css" />
    <style>
      /* Page-specific layout only. Everything visual comes from tokens — if a
         value is hardcoded here that should be a token, that is a finding. */
      body { padding: var(--s-10) var(--s-6) var(--s-24); }
      main { max-width: 68rem; margin: 0 auto; }
      h1 {
        font-size: var(--text-display);
        font-weight: var(--w-thin);
        letter-spacing: var(--track-display);
        line-height: var(--lh-display);
        margin: 0 0 var(--s-4);
      }
      h2 {
        font-size: var(--text-lg);
        font-weight: var(--w-medium);
        letter-spacing: var(--track-tight);
        margin: var(--s-16) 0 var(--s-5);
        padding-bottom: var(--s-3);
        border-bottom: 1px solid var(--rule);
      }
      .lede { color: var(--fg-dim); font-size: var(--text-md); max-width: 42rem; margin: 0; }
      .swatches { display: grid; grid-template-columns: repeat(auto-fill, minmax(7rem, 1fr)); gap: var(--s-2); }
      .sw { border: 1px solid var(--hairline); border-radius: var(--r-sm); overflow: hidden; }
      .sw div { height: 3.5rem; }
      .sw p { margin: 0; padding: var(--s-2); border-top: 1px solid var(--hairline); }
      .rows { display: grid; gap: var(--s-3); }
      .row { display: flex; align-items: baseline; gap: var(--s-4); }
      .row .label { min-width: 8rem; }
      .card {
        background: var(--bg-raised);
        border: 1px solid var(--hairline);
        border-radius: var(--r-md);
        padding: var(--s-5);
      }
      .grid2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(18rem, 1fr)); gap: var(--s-4); }
      .note { border-left: 3px solid var(--warn-line); background: var(--warn-bg); color: var(--warn-text); padding: var(--s-3) var(--s-4); border-radius: var(--r-sm); }
      .bad { border-left: 3px solid var(--danger-line); background: var(--danger-bg); color: var(--danger-text); padding: var(--s-3) var(--s-4); border-radius: var(--r-sm); }
      .good { border-left: 3px solid var(--ok-line); background: var(--ok-bg); color: var(--ok-text); padding: var(--s-3) var(--s-4); border-radius: var(--r-sm); }
      button {
        font: inherit; font-size: var(--text-sm);
        background: transparent; color: var(--fg-dim);
        border: 1px solid var(--rule); border-radius: var(--r-sm);
        padding: var(--s-2) var(--s-3); cursor: pointer;
        transition: color var(--dur-1) var(--ease-out), border-color var(--dur-1) var(--ease-out), transform var(--dur-1) var(--ease-spring);
      }
      button:hover { color: var(--fg); border-color: var(--fg); transform: translateY(-1px); }
      button.is-active { background: var(--fg); color: var(--bg); border-color: var(--fg); }
      code { font-family: var(--font-mono); font-size: var(--text-xs); color: var(--fg-dim); }
    </style>
  </head>
  <body>
    <main>
      <p class="label">achroma 0.1.0 — token reference</p>
      <h1>Achromatic.</h1>
      <p class="lede">
        Black, white and greys, with hue reserved for meaning. This page renders
        every token. Toggle your OS appearance to check both modes — nothing here
        switches themes on its own, by design.
      </p>

      <h2>The ramp</h2>
      <div class="swatches" id="ramp"></div>

      <h2>Aliases</h2>
      <div class="swatches" id="aliases"></div>

      <h2>Semantics — the only colour</h2>
      <div class="grid2">
        <div class="good">ok — a state that succeeded</div>
        <div class="note">warn — read this before trusting the output</div>
        <div class="bad">danger — this failed</div>
      </div>

      <h2>Type</h2>
      <div class="rows" id="type"></div>

      <h2>Surfaces and interaction</h2>
      <div class="grid2">
        <div class="card">
          <p class="label">Card on raised</p>
          <p style="margin: var(--s-2) 0 var(--s-4)">
            Hairline border, 4px radius, tonal depth rather than shadow.
          </p>
          <button class="is-active" type="button">Active</button>
          <button type="button">Default</button>
        </div>
        <div class="card" style="background: var(--bg-sunken)">
          <p class="label">Card on sunken</p>
          <p style="margin: var(--s-2) 0 0">
            <code>--bg-sunken</code> reads as recessed in both modes.
          </p>
        </div>
      </div>

      <h2>Grain</h2>
      <p class="lede">
        The overlay is active on this page at <code>--grain-opacity</code>. Zoom
        to 200% — a badly tiled noise texture shows its seams there and nowhere
        else.
      </p>
    </main>

    <div class="grain" aria-hidden="true"></div>

    <script type="module">
      // Reads the real computed values, so this page cannot claim a token exists
      // when it does not.
      const css = getComputedStyle(document.documentElement);
      const swatch = (name) => {
        const v = css.getPropertyValue(name).trim();
        return `<div class="sw"><div style="background:${v}"></div><p class="label">${name.slice(2)}</p></div>`;
      };
      const RAMP = [0, 25, 50, 100, 150, 200, 300, 400, 500, 600, 700, 800, 850, 900, 950, 1000];
      document.getElementById('ramp').innerHTML = RAMP.map((n) => swatch(`--n-${n}`)).join('');
      document.getElementById('aliases').innerHTML = [
        '--bg', '--bg-raised', '--bg-sunken', '--fg', '--fg-dim', '--fg-faint',
        '--hairline', '--rule',
      ].map(swatch).join('');

      const TYPE = [
        ['display', 'var(--text-display)', 'var(--w-thin)', 'var(--track-display)'],
        ['2xl', 'var(--text-2xl)', 'var(--w-light)', 'var(--track-tight)'],
        ['xl', 'var(--text-xl)', 'var(--w-light)', 'var(--track-tight)'],
        ['lg', 'var(--text-lg)', 'var(--w-regular)', 'var(--track-normal)'],
        ['md', 'var(--text-md)', 'var(--w-light)', 'var(--track-normal)'],
        ['base', 'var(--text-base)', 'var(--w-light)', 'var(--track-normal)'],
        ['sm', 'var(--text-sm)', 'var(--w-regular)', 'var(--track-normal)'],
        ['xs', 'var(--text-xs)', 'var(--w-regular)', 'var(--track-normal)'],
      ];
      document.getElementById('type').innerHTML = TYPE.map(
        ([name, size, weight, track]) =>
          `<div class="row"><span class="label">${name}</span>` +
          `<span style="font-size:${size};font-weight:${weight};letter-spacing:${track};line-height:1.1">` +
          `Extract text from any document</span></div>`,
      ).join('');
    </script>
  </body>
</html>
```

- [ ] **Step 2: Serve and look at it**

```bash
cd ~/Documents/code/achroma
python3 -m http.server 8099 &
open http://localhost:8099/proof.html
```

Check: every ramp swatch is a neutral grey with no colour cast; the display line is thin, not bold; grain is visible as texture but not as noise; the three semantic blocks are legible.

Then switch the OS appearance to dark and reload. Then zoom to 200%.

```bash
kill %1
```

- [ ] **Step 3: Commit**

```bash
cd ~/Documents/code/achroma
git add proof.html
git commit -m "feat: proof.html — the review surface for a file you cannot read"
```

---

## Task 8: The Pages workflow

**Files:**
- Create: `~/Documents/code/achroma/.github/workflows/deploy-web.yml`

- [ ] **Step 1: Write the workflow**

```yaml
name: Deploy web app to GitHub Pages

# Publishes proof.html as jvoltci.github.io/achroma/ — the living token
# reference. The repo is private; Pages is public, which needs GitHub Pro or
# Team (Pages is disabled for private repos on Free).
#
# No build step: achroma is plain CSS. The artifact is proof.html renamed to
# index.html, the stylesheet, and the fonts.

on:
  push:
    branches: [master, main]
    paths:
      - 'achroma.css'
      - 'proof.html'
      - 'fonts/**'
      - '.github/workflows/deploy-web.yml'
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/setup-node@v4
        with:
          node-version: '22'

      # The ramp assertions gate the deploy. A site that documents tokens which
      # fail their own contrast targets is worse than no site.
      - name: Assert the ramp
        run: npm test

      - name: Assemble the artifact
        run: |
          mkdir -p _site/fonts
          cp proof.html _site/index.html
          cp achroma.css _site/
          cp fonts/*.woff2 _site/fonts/
          touch _site/.nojekyll
          echo "total: $(du -sh _site | cut -f1)"

      - uses: actions/upload-pages-artifact@v3
        with:
          path: _site

  deploy:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v4
```

- [ ] **Step 2: Confirm `npm test` runs both suites**

```bash
cd ~/Documents/code/achroma
npm test
```

Expected: the 9 `oklch` oks, then both contrast tables, then `all ramp assertions passed`.

- [ ] **Step 3: Commit**

```bash
cd ~/Documents/code/achroma
git add .github/workflows/deploy-web.yml
git commit -m "ci: publish proof.html to Pages, gated on the ramp assertions"
```

---

## Task 9: Create the remote and push

**Files:** none.

- [ ] **Step 1: Create the private repo and push**

```bash
cd ~/Documents/code/achroma
gh repo create jvoltci/achroma --private --source=. --remote=origin --push
```

Expected: the repo URL, then a push summary.

- [ ] **Step 2: Enable Pages**

```bash
gh api -X POST repos/jvoltci/achroma/pages -f build_type=workflow 2>&1 | head -5
gh workflow run deploy-web.yml --repo jvoltci/achroma
```

If the first command reports that Pages is not available, the account plan does not include Pages for private repos — the npm package still works and this is not blocking.

- [ ] **Step 3: Confirm the workflow went green**

```bash
sleep 45 && gh run list --repo jvoltci/achroma --limit 3
```

Expected: a `completed` / `success` run. If it failed, read the log before proceeding: `gh run view --repo jvoltci/achroma --log-failed`.

---

## Task 10: Review gate

**Files:** none. **Stop here and get sign-off.**

- [ ] **Step 1: Present `proof.html` for review**

Confirm with the user, before publishing anything to npm:

- the ramp reads as neutral in both modes, with no colour cast
- the thin display type is the intended character
- the grain is right at `--grain-opacity` — this is the one value most likely to want taste applied
- the three semantics are distinguishable and legible

Task 11 publishes to npm. **npm versions cannot be unpublished after 72 hours and the unscoped name `achroma` is a one-time claim** — which is why this gate exists.

---

## Task 11: Publish `achroma@0.1.0`

**Files:** none.

- [ ] **Step 1: Confirm what will ship**

```bash
cd ~/Documents/code/achroma
npm pack --dry-run
```

Expected: `achroma.css`, `achroma.tailwind.css`, 6 files under `fonts/`, `NOTICE`, `README.md`, `package.json`. **`test/` and `proof.html` must be absent** — they are not part of the contract.

- [ ] **Step 2: Confirm the name is still free**

```bash
npm view achroma version 2>&1 | head -2
```

Expected: an `E404`. Anything else means the name was taken since 2026-07-30 — stop and switch to `@jvoltci/achroma`, which was also verified free.

- [ ] **Step 3: Publish**

```bash
cd ~/Documents/code/achroma
npm publish --access public
```

Expected: `+ achroma@0.1.0`

- [ ] **Step 4: Verify it installs clean from the registry**

```bash
cd /tmp && rm -rf achroma-install-probe && mkdir achroma-install-probe && cd achroma-install-probe
npm init -y >/dev/null && npm i achroma
node -e "console.log(require('fs').readFileSync('node_modules/achroma/achroma.css','utf8').length + ' bytes')"
ls node_modules/achroma/fonts/ | wc -l
```

Expected: a byte count, and `6`. This checks the `files` allowlist actually carried the fonts — a package whose CSS references fonts it did not ship fails silently, as a fallback to system sans.

- [ ] **Step 5: Tag the release**

```bash
cd ~/Documents/code/achroma
git tag v0.1.0 && git push --tags
```

---

# Phase B — naina adopts Achroma

All remaining tasks are in `~/Documents/code/naina` on branch `achroma-design-system`.

## Task 12: Wire the dependency

**Files:**
- Modify: `app/package.json`

- [ ] **Step 1: Install**

```bash
cd ~/Documents/code/naina/app
npm i achroma
```

- [ ] **Step 2: Confirm it is a registry dependency, not a path**

```bash
cd ~/Documents/code/naina/app
grep '"achroma"' package.json
```

Expected: `"achroma": "^0.1.0"`. **If this shows a `file:` path, stop** — it will resolve locally and fail in CI.

- [ ] **Step 3: Commit**

```bash
cd ~/Documents/code/naina
git add app/package.json app/package-lock.json
git commit -m "build(app): depend on achroma"
```

---

## Task 13: Rewrite `app/index.html`

**Files:**
- Modify: `app/index.html`

Every id in the DOM contract is preserved. The Google Fonts link goes (fonts now ship with Achroma, and the app claims to work offline). The grain element is added.

- [ ] **Step 1: Write the file**

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>naina — free OCR in your browser, no upload</title>
    <meta
      name="description"
      content="Extract text and structure from PDFs and images entirely in your browser. No upload, no account, no limits. Works offline after the first visit."
    />
    <link rel="icon" type="image/svg+xml" href="./favicon.svg" />
    <link rel="manifest" href="./manifest.webmanifest" />
    <!-- Both modes declared: the app follows prefers-color-scheme, so a single
         theme-color would be wrong half the time. Values are the sRGB forms of
         Achroma's --bg in each mode. -->
    <meta name="theme-color" content="#fafafa" media="(prefers-color-scheme: light)" />
    <meta name="theme-color" content="#0f0f0f" media="(prefers-color-scheme: dark)" />
    <!-- Link previews. Absolute URLs on purpose: unfurlers do not resolve relative
         paths, and the card must be a PNG because most of them will not render
         SVG (and GitHub raw serves .svg as text/plain regardless). -->
    <meta property="og:type" content="website" />
    <meta property="og:site_name" content="naina" />
    <meta property="og:url" content="https://jvoltci.github.io/naina/" />
    <meta property="og:title" content="naina — free OCR that runs in your browser" />
    <meta
      property="og:description"
      content="Read text out of PDFs and images entirely on your device. No upload, no account, no limits. Ten scripts including Devanagari. Works offline after the first visit."
    />
    <meta property="og:image" content="https://jvoltci.github.io/naina/og.png" />
    <meta property="og:image:width" content="1200" />
    <meta property="og:image:height" content="630" />
    <meta
      property="og:image:alt"
      content="naina: a scanned PDF turning into structured markdown, with pip, npm and cargo install lines"
    />

    <meta name="twitter:card" content="summary_large_image" />
    <meta name="twitter:title" content="naina — free OCR that runs in your browser" />
    <meta
      name="twitter:description"
      content="PDFs and images, read on your device. No upload, no account, no limits. Ten scripts. Works offline."
    />
    <meta name="twitter:image" content="https://jvoltci.github.io/naina/og.png" />
  </head>
  <body>
    <header class="top">
      <div class="brand">
        <span class="eye" aria-hidden="true"></span>
        <span class="wordmark">naina</span>
      </div>
      <nav>
        <a href="./doc/">Docs</a>
        <a href="https://github.com/jvoltci/naina">GitHub</a>
        <span id="offline-badge" class="badge" hidden>offline ready</span>
      </nav>
    </header>

    <main>
      <section class="hero">
        <p class="label">In-browser OCR · nothing uploaded</p>
        <h1>Extract text from any document.</h1>
        <p class="sub">
          PDFs and images, in your browser, free and without limits.
          <strong>Your file never leaves this tab</strong> — there is no server to
          send it to.
        </p>
      </section>

      <!-- Drop target doubles as the empty state. -->
      <section
        id="drop"
        class="drop"
        tabindex="0"
        role="button"
        aria-label="Choose an image, or drop one here"
      >
        <input id="file" type="file" accept="image/*,application/pdf" multiple hidden />
        <div class="drop-inner">
          <svg class="drop-icon" viewBox="0 0 24 24" aria-hidden="true">
            <path
              d="M12 16V4m0 0L8 8m4-4 4 4M4 16v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"
              fill="none"
              stroke="currentColor"
              stroke-width="1.4"
              stroke-linecap="round"
              stroke-linejoin="round"
            />
          </svg>
          <p class="drop-title">Drop a PDF or image, paste, or click to choose</p>
          <p class="drop-hint">PDF, PNG, JPEG, WebP — several files at once is fine</p>
        </div>
      </section>

      <div class="controls">
        <label class="field">
          <span class="label">Model size</span>
          <select id="tier">
            <option value="tiny" selected>Tiny — 11 MB, fastest</option>
            <option value="small">Small — 54 MB, more accurate</option>
          </select>
        </label>
        <label class="field">
          <span class="label">Script</span>
          <select id="language">
            <option value="auto" selected>Auto — detect the script</option>
            <option value="">Latin, Chinese, Japanese</option>
            <option value="arabic">Arabic, Persian, Urdu</option>
            <option value="cyrillic">Russian, Bulgarian, Serbian</option>
            <option value="devanagari">Hindi, Marathi, Nepali, Sanskrit</option>
            <option value="el">Greek</option>
            <option value="eslav">Ukrainian, Belarusian, Russian</option>
            <option value="korean">Korean</option>
            <option value="ta">Tamil</option>
            <option value="te">Telugu</option>
            <option value="th">Thai</option>
          </select>
        </label>
        <label class="field checkbox">
          <input id="show-boxes" type="checkbox" checked />
          <span>Show detected text</span>
        </label>
      </div>

      <!-- Stated up front, not in the notes at the bottom. A user who feeds it
           Hindi gets fabricated text at high confidence, and finding that out
           afterwards is too late. -->
      <p class="caveat">
        Script detection is <strong>automatic</strong> — leave it on Auto unless you
        know better. Set it explicitly and choosing wrong returns
        <strong>plausible-looking wrong text, not an error</strong>, because the
        model reads confidently in whichever alphabet you give it. Handwriting is
        unreliable. <a href="./doc/limits/">Full limitations</a>.
      </p>

      <!-- Status line: model download first, then the read itself. -->
      <section id="status" class="status" hidden>
        <div class="bar"><div id="bar-fill" class="bar-fill"></div></div>
        <p id="status-text" class="status-text"></p>
      </section>

      <nav id="pager" class="pager" aria-label="Pages" hidden></nav>

      <section id="results" class="results" hidden>
        <div class="pane">
          <div class="pane-head">
            <h2 class="label">Page</h2>
            <span id="meta" class="meta"></span>
          </div>
          <div class="canvas-wrap">
            <canvas id="canvas"></canvas>
          </div>
        </div>

        <div class="pane">
          <div class="pane-head">
            <h2 class="label">Extracted</h2>
            <div class="pane-actions">
              <button id="tab-md" class="tab is-active" type="button">Markdown</button>
              <button id="tab-txt" class="tab" type="button">Text</button>
              <button id="tab-json" class="tab" type="button">JSON</button>
              <button id="copy" class="btn" type="button">Copy</button>
              <button id="download" class="btn" type="button">Save page</button>
              <button id="download-all" class="btn" type="button" hidden>Save all</button>
            </div>
          </div>
          <pre id="output" class="output"></pre>
        </div>
      </section>

      <section id="error" class="error" hidden></section>

      <section class="notes">
        <h2>How this works</h2>
        <div class="notes-grid">
          <div>
            <h3>Nothing is uploaded</h3>
            <p>
              The whole pipeline runs in this tab, including PDF rendering. Open
              your network panel and watch — once the weights are cached, there is
              no traffic at all. No account, no quota, no upload.
            </p>
          </div>
          <div>
            <h3>Works offline</h3>
            <p>
              Weights are cached by content hash on first use. Come back with no
              connection and it still reads.
            </p>
          </div>
          <div>
            <h3>The same code as the server</h3>
            <p>
              Detection post-processing, text decoding, reading order and markdown
              assembly are the same C++ that naina's Python and Node packages run —
              compiled to WebAssembly, not rewritten in JavaScript.
            </p>
          </div>
          <div>
            <h3>What it cannot do</h3>
            <p>
              Handwriting is weak, and scripts outside PP-OCRv6's character set
              (Devanagari among them) come back as confident nonsense rather than
              an error. Latin and CJK print are its strengths.
            </p>
          </div>
        </div>
      </section>
    </main>

    <footer>
      <p>
        Apache-2.0 · PP-OCRv6 and PP-DocLayout weights, mirrored and hash-pinned ·
        <a href="https://github.com/jvoltci/naina">source</a>
      </p>
    </footer>

    <div class="grain" aria-hidden="true"></div>

    <script type="module" src="/src/main.ts"></script>
  </body>
</html>
```

- [ ] **Step 2: Verify no id was lost**

```bash
cd ~/Documents/code/naina/app
for id in drop file tier language show-boxes status status-text bar-fill pager \
          results canvas output meta error offline-badge tab-md tab-txt tab-json \
          copy download download-all; do
  grep -q "id=\"$id\"" index.html || echo "MISSING: #$id"
done
echo "id check done"
```

Expected: `id check done` with no `MISSING` lines.

- [ ] **Step 3: Commit**

```bash
cd ~/Documents/code/naina
git add app/index.html
git commit -m "feat(app): restructure markup for Achroma, drop the webfont request"
```

---

## Task 14: Rewrite `app/src/styles.css`

**Files:**
- Modify: `app/src/styles.css`

- [ ] **Step 1: Write the file**

```css
/* naina web app — Achroma.
 *
 * Every colour, size, radius and duration is a token. A hardcoded value here is
 * a bug: it means the design system is missing something, and the next site will
 * hit the same gap.
 *
 * The one deliberate exception is the confidence overlay drawn on the canvas by
 * main.ts, which runs green to amber by line confidence. That is data, not
 * decoration — deleting it would remove the signal that tells a reader which
 * lines to distrust.
 */

@import 'achroma/achroma.css';

:root {
  --maxw: 1240px;
}

a {
  color: var(--fg);
  text-decoration: underline;
  text-decoration-color: var(--rule);
  text-underline-offset: 0.2em;
  transition: text-decoration-color var(--dur-1) var(--ease-out);
}
a:hover {
  text-decoration-color: var(--fg);
}

/* ── header ─────────────────────────────────────────────────────────── */

.top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: var(--s-4);
  max-width: var(--maxw);
  margin: 0 auto;
  padding: var(--s-5) var(--s-6);
  border-bottom: 1px solid var(--hairline);
}

.brand {
  display: flex;
  align-items: center;
  gap: var(--s-2);
}

/* A stylised eye: naina means eyes. Ink, not an accent hue. */
.eye {
  width: 22px;
  height: 22px;
  border-radius: 50% 50% 50% 50% / 60% 60% 40% 40%;
  border: 1.5px solid var(--fg);
  position: relative;
  display: inline-block;
}
.eye::after {
  content: '';
  position: absolute;
  inset: 32% 34%;
  border-radius: 50%;
  background: var(--fg);
}

.wordmark {
  font-size: var(--text-lg);
  font-weight: var(--w-medium);
  letter-spacing: var(--track-tight);
}

.top nav {
  display: flex;
  align-items: center;
  gap: var(--s-5);
  font-size: var(--text-sm);
}
.top nav a {
  color: var(--fg-dim);
  text-decoration: none;
}
.top nav a:hover {
  color: var(--fg);
}

.badge {
  font-family: var(--font-mono);
  font-size: var(--text-2xs);
  font-weight: var(--w-medium);
  letter-spacing: var(--track-label);
  text-transform: uppercase;
  padding: var(--s-1) var(--s-2);
  border-radius: var(--r-sm);
  border: 1px solid var(--ok-line);
  background: var(--ok-bg);
  color: var(--ok-text);
}

/* ── layout ─────────────────────────────────────────────────────────── */

main {
  max-width: var(--maxw);
  margin: 0 auto;
  padding: 0 var(--s-6) var(--s-16);
}

.hero {
  padding: var(--s-16) 0 var(--s-12);
  max-width: 46rem;
}

.hero .label {
  display: block;
  margin: 0 0 var(--s-4);
}

/* Thin and huge, against the tiny wide label above it. This pairing is the
   signature; a heavy weight here undoes it. */
.hero h1 {
  margin: 0 0 var(--s-4);
  font-size: var(--text-display);
  font-weight: var(--w-thin);
  letter-spacing: var(--track-display);
  line-height: var(--lh-display);
}

.sub {
  margin: 0;
  font-size: var(--text-md);
  color: var(--fg-dim);
  max-width: 38rem;
}
.sub strong {
  color: var(--fg);
  font-weight: var(--w-regular);
}

/* ── drop zone ──────────────────────────────────────────────────────── */

.drop {
  border: 1px dashed var(--rule);
  border-radius: var(--r-md);
  background: var(--bg-raised);
  padding: var(--s-16) var(--s-6);
  text-align: center;
  cursor: pointer;
  transition:
    border-color var(--dur-2) var(--ease-out),
    background var(--dur-2) var(--ease-out),
    transform var(--dur-2) var(--ease-spring);
}
.drop:hover,
.drop:focus-visible {
  border-color: var(--fg);
  outline: none;
}
.drop.is-over {
  border-color: var(--fg);
  border-style: solid;
  background: var(--bg-sunken);
  transform: translateY(-2px);
}

.drop-icon {
  width: 32px;
  height: 32px;
  color: var(--fg-dim);
  margin-bottom: var(--s-3);
}

.drop-title {
  margin: 0 0 var(--s-1);
  font-size: var(--text-md);
  font-weight: var(--w-regular);
  color: var(--fg);
  letter-spacing: var(--track-tight);
}
.drop-hint {
  margin: 0;
  font-size: var(--text-sm);
  color: var(--fg-faint);
}

/* ── controls ───────────────────────────────────────────────────────── */

.controls {
  display: flex;
  flex-wrap: wrap;
  gap: var(--s-6);
  align-items: end;
  margin: var(--s-6) 0 0;
}

.field {
  display: flex;
  flex-direction: column;
  gap: var(--s-2);
}

.field select {
  background: var(--bg-raised);
  color: var(--fg);
  border: 1px solid var(--rule);
  border-radius: var(--r-sm);
  padding: var(--s-2) var(--s-3);
  font: inherit;
  font-size: var(--text-sm);
  transition: border-color var(--dur-1) var(--ease-out);
}
.field select:hover {
  border-color: var(--fg);
}

.field.checkbox {
  flex-direction: row;
  align-items: center;
  gap: var(--s-2);
  cursor: pointer;
  padding-bottom: var(--s-2);
  font-size: var(--text-sm);
  color: var(--fg-dim);
}
.field.checkbox input {
  accent-color: var(--fg);
  width: 15px;
  height: 15px;
}

/* Up-front caveat about supported scripts. Deliberately prominent: a user who
   discovers the limit after trusting the output has already been misled. This
   is one of the three places colour is allowed, because it carries meaning. */
.caveat {
  margin: var(--s-6) 0 0;
  padding: var(--s-3) var(--s-4);
  border: 1px solid var(--warn-line);
  border-left: 3px solid var(--warn-line);
  border-radius: var(--r-sm);
  background: var(--warn-bg);
  font-size: var(--text-sm);
  color: var(--warn-text);
}
.caveat strong {
  font-weight: var(--w-medium);
}
.caveat a {
  color: var(--warn-text);
}

/* ── status ─────────────────────────────────────────────────────────── */

.status {
  margin: var(--s-6) 0 0;
}

.bar {
  height: 2px;
  background: var(--hairline);
  overflow: hidden;
}

.bar-fill {
  height: 100%;
  width: 0;
  background: var(--fg);
  transition: width var(--dur-3) var(--ease-out);
}

.bar-fill.is-indeterminate {
  animation: sweep 1.1s var(--ease-out) infinite;
  transform-origin: left center;
}

@keyframes sweep {
  0% {
    transform: scaleX(0.05) translateX(0);
  }
  50% {
    transform: scaleX(0.4) translateX(150%);
  }
  100% {
    transform: scaleX(0.05) translateX(2000%);
  }
}

.status-text {
  margin: var(--s-3) 0 0;
  font-family: var(--font-mono);
  font-size: var(--text-xs);
  color: var(--fg-dim);
}

/* ── results ────────────────────────────────────────────────────────── */

.results {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: var(--s-4);
  margin: var(--s-10) 0 0;
}

@media (max-width: 900px) {
  .results {
    grid-template-columns: 1fr;
  }
}

.pane {
  border: 1px solid var(--hairline);
  border-radius: var(--r-md);
  background: var(--bg-raised);
  overflow: hidden;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.pane-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: var(--s-4);
  flex-wrap: wrap;
  padding: var(--s-3) var(--s-4);
  border-bottom: 1px solid var(--hairline);
  background: var(--bg-sunken);
}

.pane-head h2 {
  margin: 0;
}

.meta {
  font-family: var(--font-mono);
  font-size: var(--text-2xs);
  color: var(--fg-faint);
}

.pane-actions {
  display: flex;
  gap: var(--s-1);
}

.tab,
.btn {
  background: transparent;
  border: 1px solid var(--rule);
  color: var(--fg-dim);
  border-radius: var(--r-sm);
  padding: var(--s-1) var(--s-2);
  font: inherit;
  font-size: var(--text-xs);
  cursor: pointer;
  transition:
    color var(--dur-1) var(--ease-out),
    border-color var(--dur-1) var(--ease-out),
    background var(--dur-1) var(--ease-out);
}
.tab:hover,
.btn:hover {
  color: var(--fg);
  border-color: var(--fg);
}
.tab.is-active {
  color: var(--bg);
  background: var(--fg);
  border-color: var(--fg);
  font-weight: var(--w-medium);
}

/* A neutral checkerboard, so a transparent PNG is legible without introducing
   a hue. */
.canvas-wrap {
  padding: var(--s-4);
  overflow: auto;
  max-height: 70vh;
  background: repeating-conic-gradient(
      var(--bg-sunken) 0% 25%,
      var(--bg-raised) 0% 50%
    )
    50% / 18px 18px;
}

canvas {
  display: block;
  max-width: 100%;
  height: auto;
  border-radius: var(--r-sm);
}

/* The output pane shows recognised text, which may be Devanagari, Arabic,
   Greek, Korean, Tamil, Telugu or Thai — none of which Geist covers. The
   fallback chain in --font-mono handles those per-glyph. */
.output {
  margin: 0;
  padding: var(--s-4);
  overflow: auto;
  max-height: 70vh;
  font-family: var(--font-mono);
  font-size: var(--text-sm);
  line-height: var(--lh-body);
  white-space: pre-wrap;
  word-break: break-word;
  color: var(--fg);
  tab-size: 2;
}

.error {
  margin: var(--s-6) 0 0;
  padding: var(--s-3) var(--s-4);
  border: 1px solid var(--danger-line);
  border-left: 3px solid var(--danger-line);
  background: var(--danger-bg);
  color: var(--danger-text);
  border-radius: var(--r-sm);
  font-size: var(--text-sm);
}

/* ── pager (multi-page documents) ───────────────────────────────────── */

.pager {
  display: flex;
  flex-wrap: wrap;
  gap: var(--s-1);
  margin: var(--s-10) 0 0;
}

.page-chip {
  min-width: 2rem;
  padding: var(--s-1) var(--s-2);
  border: 1px solid var(--rule);
  border-radius: var(--r-sm);
  background: var(--bg-raised);
  color: var(--fg-dim);
  font: inherit;
  font-family: var(--font-mono);
  font-size: var(--text-xs);
  cursor: pointer;
  transition:
    color var(--dur-1) var(--ease-out),
    border-color var(--dur-1) var(--ease-out),
    background var(--dur-1) var(--ease-out);
}
.page-chip:hover {
  color: var(--fg);
  border-color: var(--fg);
}
.page-chip.is-active {
  background: var(--fg);
  border-color: var(--fg);
  color: var(--bg);
  font-weight: var(--w-medium);
}
/* A page that failed still gets a chip — silently dropping it would leave the
   user counting pages to work out which one is missing. */
.page-chip.is-failed {
  border-color: var(--danger-line);
  color: var(--danger-text);
}
.page-chip.is-failed.is-active {
  background: var(--danger-text);
  border-color: var(--danger-text);
  color: var(--n-0);
}

/* ── notes ──────────────────────────────────────────────────────────── */

.notes {
  margin: var(--s-24) 0 0;
  padding-top: var(--s-10);
  border-top: 1px solid var(--rule);
}

.notes h2 {
  margin: 0 0 var(--s-8);
  font-size: var(--text-xl);
  font-weight: var(--w-thin);
  letter-spacing: var(--track-tight);
}

.notes-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(15rem, 1fr));
  gap: var(--s-8) var(--s-10);
}

.notes h3 {
  margin: 0 0 var(--s-2);
  font-size: var(--text-sm);
  font-weight: var(--w-medium);
  color: var(--fg);
}

.notes p {
  margin: 0;
  font-size: var(--text-sm);
  color: var(--fg-dim);
}

footer {
  border-top: 1px solid var(--hairline);
  margin-top: var(--s-16);
}

footer p {
  max-width: var(--maxw);
  margin: 0 auto;
  padding: var(--s-6);
  font-size: var(--text-xs);
  color: var(--fg-faint);
}
```

- [ ] **Step 2: Build**

```bash
cd ~/Documents/code/naina/app
npm run build
```

Expected: `tsc --noEmit` clean, then a Vite build summary. A failure to resolve `achroma/achroma.css` means the `exports` map in Task 1 is wrong.

- [ ] **Step 3: Confirm the fonts were bundled, not fetched**

```bash
cd ~/Documents/code/naina/app
ls dist/assets/*.woff2 | wc -l
grep -rl "fonts.googleapis\|fonts.gstatic" dist/ | head -3
```

Expected: a non-zero woff2 count, and **no output** from the grep. A Google Fonts reference in `dist/` would break the offline claim quietly.

- [ ] **Step 4: Commit**

```bash
cd ~/Documents/code/naina
git add app/src/styles.css
git commit -m "feat(app): restyle onto Achroma tokens"
```

---

## Task 15: Verify the app still works

**Files:** none.

- [ ] **Step 1: Run the e2e suite**

```bash
cd ~/Documents/code/naina/app
NAINA_E2E_IMAGE=$(ls test/*.png 2>/dev/null | head -1) node test/e2e.mjs
```

If no fixture image exists in `test/`, point `NAINA_E2E_IMAGE` at any page screenshot. Expected: every `ok` line, no failures.

- [ ] **Step 2: Check the shape of the output, not that output appeared**

The suite asserts text came back. Read the `#meta` line it prints and confirm **line count, region count and mean confidence are all non-zero.** A read returning 33 lines at 0.99 with zero layout regions is a regression this suite would otherwise pass.

- [ ] **Step 3: Mutation-check `.page-chip`**

This is the one contract element that fails quietly, so prove the assertion is real:

```bash
cd ~/Documents/code/naina/app
sed -i '' 's/^\.page-chip {/.page-chip-renamed {/' src/styles.css
npm run build >/dev/null 2>&1
NAINA_E2E_IMAGE=$(ls test/*.png 2>/dev/null | head -1) node test/e2e.mjs; echo "exit=$?"
```

Expected: `exit=1`. The class only styles chips — `main.ts` still sets the class name, so the *selector* count in Playwright is what must not change. If this passes, the pager assertion is not load-bearing and the e2e suite needs fixing before you trust it.

```bash
cd ~/Documents/code/naina/app
sed -i '' 's/^\.page-chip-renamed {/.page-chip {/' src/styles.css
npm run build >/dev/null 2>&1
```

- [ ] **Step 4: Commit nothing; report**

No code changed in this task. If step 3 did not fail as expected, stop and say so rather than proceeding.

---

## Task 16: Verify non-Latin output is still legible

**Files:** none.

Geist covers latin, latin-ext and cyrillic. Devanagari, Arabic, Greek, Korean, Tamil, Telugu and Thai fall back per-glyph. This is the check that the fallback chain works rather than rendering boxes.

- [ ] **Step 1: Run the Hindi check**

```bash
cd ~/Documents/code/naina/app
node test/hindi-check.mjs
```

Expected: the `#meta` and `#output` contents. **Confirm actual Devanagari characters appear in the output** — not `?`, not `□`, not Latin transliteration.

- [ ] **Step 2: Confirm the glyphs render rather than merely existing in the DOM**

Text can be present in the DOM and still draw as tofu boxes. Screenshot the output pane and look at it:

```bash
cd ~/Documents/code/naina/app
node -e "
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage();
  await p.goto('http://localhost:4173/');
  await p.evaluate(() => {
    document.getElementById('results').hidden = false;
    document.getElementById('output').textContent =
      'नैना — देवनागरी\nΕλληνικά\nالعربية\nதமிழ்\nతెలుగు\nไทย\n한국어\nРусский';
  });
  await p.locator('#output').screenshot({ path: 'scripts-check.png' });
  await b.close();
})();
"
open scripts-check.png
```

Run `npm run preview` in another shell first. Expected: eight legible lines, no boxes. Delete `scripts-check.png` afterwards.

---

## Task 17: Look at all four states

**Files:** none.

The browser suites cannot judge whether it looks right.

- [ ] **Step 1: Serve the build**

```bash
cd ~/Documents/code/naina/app
npm run preview
```

- [ ] **Step 2: Check each state**

- **Light** (default) — display type thin, hairlines visible but not glaring, grain present as texture
- **Dark** — switch the OS appearance and reload. Confirm the caveat and error blocks are still legible, and that the checkerboard behind a transparent image has not inverted into something loud
- **900px wide** — results collapse to one column; the pane heads still fit
- **200% zoom** — the grain must not show tiling seams; this is the only place a bad noise texture is obvious

- [ ] **Step 3: Run a real document end to end**

Drop a multi-page PDF. Confirm: the pager appears, chips are clickable, a failed page still gets a chip in `--danger-text`, `Save all` appears, `Copy` flips to `Copied`, and the confidence overlay still draws **green to amber** — that colour is data and must not have gone neutral.

- [ ] **Step 4: Commit any tuning**

```bash
cd ~/Documents/code/naina
git add -A app/
git commit -m "style(app): tune spacing and weight after looking at it"
```

Skip if nothing changed.

---

## Task 18: Update the spec's status

**Files:**
- Modify: `docs/design/specs/2026-07-30-achroma-design-system-design.md:3`

- [ ] **Step 1: Change the status line**

Replace:

```markdown
**Status:** designed, not implemented.
```

with:

```markdown
**Status:** implemented for Achroma and naina. The `tool` cycle is still open.
```

- [ ] **Step 2: Commit**

```bash
cd ~/Documents/code/naina
git add docs/design/specs/2026-07-30-achroma-design-system-design.md
git commit -m "docs: mark the Achroma spec implemented for naina"
```

---

## Self-review notes

**Spec coverage.** Every section of the spec maps to a task: vocabulary and decisions → the README in Task 1; the ramp and aliases → Task 5; semantics ×3 → Task 5; the interface-vs-content rule → the comment headers in Tasks 5 and 14 plus the overlay check in Task 17 step 3; token inventory → Task 5; architecture and the two consumers → Tasks 5, 6 and 12; fonts → Tasks 4 and 5; the four verification items → Tasks 2, 3, 5 (steps 3–4), 15, 16 and 17; the ordering constraint → Task 11 before Task 12.

**Deliberately deferred, per the spec's "Out of scope":** the `tool` adoption and its 685 utility occurrences, the achromatic chart treatment, the mkdocs docs site, and `tool`'s two pre-existing bugs. None of these are in this plan.

**Known gap.** `--fg-faint` is asserted at ≥3:1 and is therefore valid for non-text and large text only, but it is used for `.meta`, `.drop-hint` and the footer at `--text-2xs` and `--text-xs` — which are small. Those are secondary, non-essential strings, so this is a considered trade rather than an oversight; if a reviewer disagrees, the fix is to move them to `--fg-dim` and re-run Task 5 step 2. Flagged here rather than left to be discovered.
