# Achroma — an achromatic design system

**Status:** designed, not implemented.
**Scope of this spec:** the token package, and naina's web app adopting it.
The sibling `tool` repo is a separate cycle — see "Out of scope".

## The problem

Two things are wanted, and they are not the same thing:

1. naina's web app should look better — cleaner, more deliberate, minimal.
2. The look should be **reusable across every site**, present and future, as a
   signature style rather than a one-off stylesheet.

(2) is the constraint that matters. A stylesheet written into `app/src/styles.css`
cannot be reused; site #2 copies it and the two drift. CLAUDE.md already records
that exact failure twice — a WASM binding with its own staging plan, and a web app
with its own script detection. So the deliverable is a **token package**, and
naina is its first consumer, not its owner.

## Vocabulary

The requested look is black, white and greys. The precise word for that is
**achromatic** — zero hue. "Chromatic" means the opposite. Related terms, so the
system is named correctly once:

| Term | Means |
|---|---|
| **Achromatic** | No hue. Black, white, greys. What this is. |
| **Monochrome** | *One* hue plus its tints and shades. Not this. |
| **Greyscale** | The implementation fact: R = G = B. |
| **Neutral ramp** | The ordered grey ladder everything is built from. |

Working name: **Achroma**. Three things depend on the string — the directory, the
package name, and one import line per consumer — so renaming it later is cheap but
not free.

## Decisions

| Question | Decision |
|---|---|
| Hue policy | Achromatic + **semantic hue only**. Greys carry all structure, hierarchy, brand and interaction. Hue appears only where it *means* something. |
| Where it lives | Its **own repo**, `jvoltci/achroma`, published to npm as `@jvoltci/achroma` with zero dependencies. Not in naina — naina is a C++ OCR library, and every future site would otherwise depend on it to get its greys. |
| Character | Editorial/Swiss skeleton + an atmospheric layer. |
| Type | **Geist + Geist Mono**, self-hosted, variable. |
| Modes | Both. **Light canonical**, dark derived. |
| naina's default | Light, with dark on `prefers-color-scheme`. |

## The character

Achromatic + minimal is the most crowded aesthetic on the web. Without one
deliberate move it reads as default grey. Since hue is unavailable, the character
comes from four things:

**1. Extreme type contrast.** Display type large and *thin*; labels tiny and
*wide*. The tension between the two is the signature.

```
Display   clamp(2.5rem, 6vw, 5rem)   weight 200   tracking -0.035em
Label     0.6875rem  uppercase mono  weight 500   tracking +0.14em
```

Heavy display weight is the dated tell. Thin at large sizes is the current move,
and it only works with a face drawn for it — hence Geist.

**2. Never pure black on pure white.** `#fff` on `#000` is the amateur signal.
Paper is `oklch(0.985 0 0)`, ink is `oklch(0.16 0 0)`. The ramp is OKLCH so the
steps are perceptually even, which greyscale in sRGB hex is not.

**3. Film grain.** One fixed full-viewport overlay: a tiled SVG `feTurbulence` at
2.5% opacity, `pointer-events: none`. This is the ingredient that makes an
achromatic surface read as *material* rather than flat. One element, no
JavaScript, disproportionate effect.

**4. Visible hairline structure.** 1px rules, 2px radius, structure used as the
only ornament. Motion is spring-curved, staggered 40ms, hover travel 1–2px.

## The ramp

Chroma is exactly `0` and hue exactly `0` on every neutral. That is the machine
-checkable definition of achromatic, and the package asserts it (see
"Verification").

**The ramp is absolute; the aliases are what flip.** Sixteen steps, chroma `0`,
hue `0`, mode-independent. Both modes index into the same ladder — so dark mode is
about ten lines of re-pointing rather than a second palette to keep in sync.

```css
--n-0:    oklch(1.000 0 0);   --n-500:  oklch(0.620 0 0);
--n-25:   oklch(0.985 0 0);   --n-600:  oklch(0.520 0 0);
--n-50:   oklch(0.968 0 0);   --n-700:  oklch(0.400 0 0);
--n-100:  oklch(0.945 0 0);   --n-800:  oklch(0.300 0 0);
--n-150:  oklch(0.922 0 0);   --n-850:  oklch(0.220 0 0);
--n-200:  oklch(0.900 0 0);   --n-900:  oklch(0.170 0 0);
--n-300:  oklch(0.840 0 0);   --n-950:  oklch(0.130 0 0);
--n-400:  oklch(0.720 0 0);   --n-1000: oklch(0.090 0 0);
```

| Alias | Light | Dark |
|---|---|---|
| `--bg` | `n-25` | `n-900` |
| `--bg-raised` | `n-0` | `n-850` |
| `--bg-sunken` | `n-50` | `n-950` |
| `--fg` | `n-950` | `n-50` |
| `--fg-dim` | `n-600` | `n-400` |
| `--fg-faint` | `n-500` | `n-500` |
| `--hairline` | `n-150` | `n-800` |
| `--rule` | `n-300` | `n-700` |

Dark is **not** a naive inversion: the range is compressed and the black lifted to
`n-900`, because inverted hairlines at full contrast glare.

Mode switching is done twice on purpose — once under
`@media (prefers-color-scheme: dark)` and once under `.dark, [data-theme='dark']`
— because naina has no toggle and follows the OS, while `tool` uses next-themes,
which sets a class. `light-dark()` would remove the duplication but couples the
system to `color-scheme`; for a foundation this many sites inherit, ten duplicated
lines is the cheaper trade.

### Semantic hues — the only colour

Low chroma on purpose, so they read as signal rather than decoration.

**One token per semantic is not enough,** which the arithmetic established rather
than taste. A single amber that is bright enough to read as a warning border
cannot also be dark enough for body text: `oklch(0.62 0.13 75)` on paper computes
to **3.57:1**, short of the 4.5:1 an earlier draft of this spec asserted. Amber on
white never clears AA at display saturation — that is a property of the hue, not a
tuning mistake. So each semantic gets three tokens with three different jobs:

| | `-text` (≥4.5:1) | `-line` (≥3:1) | `-bg` (subtle fill) |
|---|---|---|---|
| **danger** light | `oklch(0.50 0.19 27)` | `oklch(0.62 0.17 27)` | `oklch(0.965 0.015 27)` |
| **danger** dark | `oklch(0.72 0.16 25)` | `oklch(0.50 0.15 27)` | `oklch(0.240 0.045 27)` |
| **warn** light | `oklch(0.50 0.11 75)` | `oklch(0.72 0.13 75)` | `oklch(0.968 0.022 85)` |
| **warn** dark | `oklch(0.82 0.13 82)` | `oklch(0.55 0.11 78)` | `oklch(0.240 0.040 80)` |
| **ok** light | `oklch(0.48 0.12 150)` | `oklch(0.62 0.12 150)` | `oklch(0.965 0.018 150)` |
| **ok** dark | `oklch(0.78 0.13 155)` | `oklch(0.52 0.11 152)` | `oklch(0.230 0.040 152)` |

These are hand-computed estimates. The script in "Verification" is the authority;
whatever misses its target gets retuned before ship.

## The rule that keeps this honest

**Achromatic governs the interface. It never governs content.**

Where colour is *data*, it stays. Two live cases:

- naina's confidence overlay, `hue = 120 × confidence`
  ([`app/src/main.ts:174`](../../../app/src/main.ts)) — green→amber is how a user
  sees which lines to distrust. Deleting it would remove a signal, which is the
  precise failure mode CLAUDE.md is organised around.
- the annotation swatches in `tool`'s PDF editor — a yellow highlighter has to be
  yellow.

Anything a user *chose* or a document *contains* is out of the system's reach.

## Token inventory

One file, plain CSS custom properties, no build step.

| Group | Tokens |
|---|---|
| Colour | the ramp above; `--bg`, `--bg-raised`, `--bg-sunken`, `--fg`, `--fg-dim`, `--fg-faint`, `--hairline`, `--rule`, `--danger`, `--warn`, `--ok` |
| Type | `--font-sans`, `--font-mono`; `--text-2xs` 0.6875rem → `--text-2xl` 1.875rem; `--text-display` clamp; weights 200/300/400/500; `--track-display` −0.035em, `--track-tight` −0.015em, `--track-label` +0.14em |
| Space | 4px base: `--s-1` 4px … `--s-16` 128px |
| Radius | `--r-0` 0, `--r-sm` 2px, `--r-md` 4px, `--r-lg` 8px. Nothing rounder — near-sharp is the character. |
| Motion | `--ease-spring` `cubic-bezier(0.16,1,0.3,1)`, `--ease-out` `cubic-bezier(0.33,1,0.68,1)`; `--dur-1` 120ms … `--dur-4` 420ms; `--stagger` 40ms |
| Texture | `--grain-opacity` 0.025 |

## Architecture

A new repository, `jvoltci/achroma`, at `~/Documents/code/achroma`. **Private repo,
public Pages** — the pattern already used for `studio`. Published to npm as
unscoped **`achroma`** (verified available 2026-07-30, as was `@jvoltci/achroma`).

```
achroma/
  package.json           name: achroma · "dependencies": {} · files: css, fonts
  achroma.css            tokens, both modes, small base layer, .grain, .label
  achroma.tailwind.css   @theme inline bridge → shadcn's variable names
  fonts/                 6 vendored woff2 (Geist + Geist Mono, latin/latin-ext/cyrillic)
  proof.html             every token rendered; the visual test surface
  test/oklch.mjs         OKLCH → linear sRGB → relative luminance
  test/oklch.test.mjs    that math, against known sRGB primaries
  test/contrast.mjs      parses achroma.css and asserts the ramp
  .github/workflows/deploy-web.yml
  README.md  LICENSE  NOTICE
```

**Zero dependencies, no build step, no JavaScript at runtime.** Plain CSS custom
properties are the only thing every target stack eats natively, so React, Next,
Vite, Astro and a bare `.html` all consume the identical file.

- **naina/app** — Vite, plain CSS. `npm i achroma`, then `@import 'achroma/achroma.css'`
  ahead of `styles.css`.
- **tool** — Next 15, Tailwind v4, shadcn. Imports `achroma.css` plus the bridge,
  which remaps `--primary`, `--border`, `--ring`, `--card` and the rest onto
  Achroma values. Radix components inherit with no edits to their own files.
  Cycle 3.
- **no-tooling pages** — `unpkg.com/achroma/achroma.css`, or copy the file.

`proof.html` exists because a token file cannot be reviewed by reading it. It
renders the full ramp, every type step, both modes side by side, the grain on and
off, and each component pattern. The Pages workflow publishes it as
`jvoltci.github.io/achroma/`, which makes it the living reference rather than a
local scratch file.

### Ordering constraint

naina's CI checks out only naina ([`deploy-web.yml:37-73`](../../../.github/workflows/deploy-web.yml)),
so a `file:` dependency on a sibling directory resolves locally and **fails in
Actions**. Achroma must therefore be published *before* naina's `package.json`
references it. Publishing is the last step of the Achroma cycle, gated on review
of `proof.html` — not the first step of naina's.

## naina adopts it

`app/index.html` and `app/src/styles.css` are rewritten. `main.ts`, `pages.ts`
and `ocr.worker.ts` are **not touched** — this is presentation only.

### Hard contract

The browser tests address the DOM by id, and one by class. These are not
renameable:

```
#drop  #file  #language  #tier  #show-boxes  #status  #status-text  #bar-fill
#pager  #results  #canvas  #output  #meta  #error  #offline-badge
#tab-md  #tab-txt  #tab-json  #copy  #download  #download-all
.page-chip            ← e2e.mjs queries '#pager .page-chip'
```

Every one is also queried by `main.ts` via `getElementById`. A rename fails
loudly at runtime, but `.page-chip` would only fail in the Playwright suite —
worth noting because that is the quietest of the failure modes here.

### Functionality that must survive, and is checked

Drop-zone click / Enter / Space / drag-hover / drop / paste-anywhere · multi-file
· tier select + `localStorage` · language select, 11 options including `auto`, +
`localStorage` · show-boxes toggle redrawing the overlay · the script caveat,
still prominent · determinate and indeterminate progress bar · pager chips with
active and **failed** states · two result panes · meta line · md/txt/json tabs ·
copy with its "Copied" confirmation · save page · save all, hidden below two
pages · error block · the four notes · footer · offline badge, hidden until the
service worker registers · results collapsing to one column at 900px.

### What changes visually

| Now | After |
|---|---|
| Dark only, `#0d1117`, blue `#4ea1ff` accent everywhere | Light canonical on paper `oklch(0.985 0 0)`; dark on system preference; no accent hue |
| Outfit 300 / JetBrains Mono | Geist 200–500 / Geist Mono |
| `h1` at weight 700, 3.1rem max | Display at weight 200, 5rem max, tracking −0.035em |
| Radius 12px throughout | 2–8px; near-sharp |
| Blue-tinted radial gradient on `body` | Grain overlay; long neutral tonal gradient |
| Field labels 0.85rem sentence case | Uppercase mono micro-labels, +0.14em |
| Amber caveat, blue links, green badge | Caveat keeps `--warn`; links and badge go neutral; `--ok` reserved for state |
| Tabs: active tab filled blue | Active tab filled ink |

The confidence overlay in `main.ts` is deliberately untouched.

## Verification

CLAUDE.md's rule is that failures here have always been silent, so the checks are
built to be loud.

**1. The ramp is actually achromatic.** `test/contrast.mjs` parses `achroma.css`
and fails if any token named `--n-*` has non-zero chroma. This is the whole
premise of the system; a stray `0.01` would be invisible by eye and would
propagate to every site.

**2. Contrast is asserted, not assumed.** The same script converts OKLCH → linear
sRGB → relative luminance (about 30 lines, no dependency — consistent with the
no-heavy-deps rule) and asserts, in **both** modes:

| Pair | Target | Hand-computed |
|---|---|---|
| `--fg` on `--bg` | ≥ 7:1 (AAA body) | light **19.3:1** · dark **17.0:1** |
| `--fg-dim` on `--bg` | ≥ 4.5:1 (AA body) | light **5.3:1** · dark **7.7:1** |
| `--fg-faint` on `--bg` | ≥ 3:1, non-text and large text only | light **3.5:1** |
| `--hairline` on `--bg` | ≥ 1.15:1 — a subtle divider | light **1.21:1** |
| `--rule` on `--bg` | ≥ 1.4:1 — the visible editorial rules | light **1.57:1** |
| every `*-text` on `--bg` and on its own `*-bg` | ≥ 4.5:1 | danger light **6.3:1**, warn light **5.9:1** |
| every `*-line` on `--bg` | ≥ 3:1 | — |

Because chroma is `0` on every neutral, the OKLCH→luminance chain collapses to
`Y = L³` for the whole ramp, which is why those figures can be checked by hand.
The semantics cannot, and are the reason the script exists.

The `--hairline` and `--rule` split, and the `≥1.15:1` figure, both came out of
doing this arithmetic: an earlier draft asserted a single `--hairline ≥ 1.4:1`,
which `oklch(0.922 0 0)` misses at 1.21:1. Rather than darken every divider to
satisfy a number, the token was split by job.

These are hand-computed and **the script is the authority.** Anything it reports
below target gets retuned before ship.

**3. The app still works.** Existing suites, unchanged:

```bash
cd app && npm run build && NAINA_E2E_IMAGE=<page.png> node test/e2e.mjs
```

**4. Mutation check on the contract.** Rename `.page-chip` in the stylesheet
only, confirm `e2e.mjs` fails, revert. This proves the pager assertion is load
-bearing rather than incidentally passing — the technique CLAUDE.md prefers over
asserting the design is right.

**5. Both modes are looked at.** `proof.html` and the app, light and dark, at
900px and above. Grain is checked at 200% zoom, where a badly tiled noise texture
becomes obvious.

## Out of scope

- **`tool` (cycle 3).** Measured, not estimated: **685 chromatic Tailwind
  utility occurrences across 26 files**, plus hardcoded hex in 11 more. Of those,
  `slate` 184 and `neutral` 150 are near-achromatic already and swap mechanically;
  the remaining ~350 (`green` 85, `blue` 80, `teal` 48, `emerald` 40, `sky` 24,
  `violet` 16, …) each need a call between "neutral" and "semantic". Its own spec.
  Two prerequisites:
  - **`tool` has no commits at all.** `git log` there fails with *"your current
    branch 'master' does not have any commits yet"*, over 78 files under `src/`.
    There is nothing to revert to. A baseline commit is required before anything
    in that repo is edited.
  - Its five recharts calculator pages need an achromatic chart treatment —
    lightness ramp plus pattern differentiation, since series can no longer be
    told apart by hue. That is a real dataviz problem, not a token swap.
- **The mkdocs docs site** at `/naina/doc/`. Separate theming surface.
- **`--bg-primary` / `--accent-primary` / `--font-serif`** in `tool`'s
  `globals.css`, commented "MAIA DEFAULTS (Critical for Timeline3D)". No
  `Timeline3D` exists in that repo. Pre-existing dead code; left alone and noted.
- **Pre-existing bug, `tool`.** `globals.css` maps `--font-sans` to
  `--font-geist-sans`, which is never defined anywhere, while `layout.tsx` loads
  Inter into `--font-sans`. Tailwind's `font-sans` utilities therefore resolve to
  nothing. Fix belongs in cycle 3.

## Fonts

Verified from the package `LICENSE` on 2026-07-30: Geist is **SIL Open Font
License 1.1**, which is compatible with everything this project ships. Vendored
from `@fontsource-variable/geist@5.3.0` and `-mono@5.3.0`, taking six files —
`latin`, `latin-ext` and `cyrillic` for each face, 29 KB per subset. Declared as:

```css
@font-face {
  font-family: 'Geist Variable';
  font-weight: 100 900;              /* one variable file, every weight */
  src: url('./fonts/geist-latin-wght-normal.woff2') format('woff2-variations');
  unicode-range: U+0000-00FF, /* … */;
}
```

Self-hosted, not Google Fonts: naina claims to work offline, and a webfont fetched
from a CDN would break that claim quietly rather than loudly.

**Geist does not cover the scripts naina reads.** It ships latin, latin-ext,
cyrillic and vietnamese. Devanagari, Arabic, Greek, Korean, Tamil, Telugu and Thai
are absent — and all seven can appear in `#output`, because that pane displays
recognised text. Browsers fall back per-glyph, so this works by default, but only
if the fallback chain is deliberate:

```css
--font-sans: 'Geist Variable', ui-sans-serif, system-ui, sans-serif;
--font-mono: 'Geist Mono Variable', ui-monospace, 'SF Mono', Menlo, monospace;
```

Both family strings are verified from the packages, not guessed: the sans is
`'Geist Variable'` and the mono is `'Geist Mono Variable'` — note the word order,
which is easy to get backwards and would fail silently to the system monospace.

Cyrillic is the one worth having: naina has a Cyrillic recognition model, so
including that subset means Russian output renders in Geist Mono rather than
falling back mid-document. `test/hindi-check.mjs` is what proves the Devanagari
case still renders legibly after the font change — not an assumption.

## Steps

### Phase A — the Achroma repo

1. `git init` at `~/Documents/code/achroma`. Scaffold `package.json`
   (name `achroma`, `"dependencies": {}`), `README.md`, `LICENSE`, `NOTICE`
   recording the OFL-1.1 font.
2. `test/oklch.mjs` and `test/oklch.test.mjs` **first**. The conversion math is
   asserted against known sRGB primaries before anything depends on it — a wrong
   coefficient would otherwise produce contrast numbers that are confidently
   wrong, which is this project's signature failure.
3. `test/contrast.mjs` — the chroma-is-zero assertion, then the contrast table.
4. `achroma.css` — ramp, aliases, semantics, type, space, radius, motion, both
   modes, `.grain`, `.label`, and a base layer that is deliberately small:
   `box-sizing`, margin reset, `-webkit-text-size-adjust`, `:focus-visible` ring,
   and `prefers-reduced-motion` zeroing every duration. Nothing else — an
   opinionated reset in a shared package fights each consumer's own base styles.
5. Vendor the six woff2 files and declare `@font-face`.
6. Run `test/contrast.mjs`. Retune whatever misses.
7. `proof.html`.
8. `achroma.tailwind.css` bridge. Written now, unused until cycle 3.
9. `.github/workflows/deploy-web.yml` — private repo, public Pages, serving
   `proof.html` as the site index.
10. **Review gate:** look at `proof.html` in both modes.
11. `npm publish` — `achroma@0.1.0`.

### Phase B — naina adopts it

12. `npm i achroma` in `app/`, import ahead of `styles.css`.
13. Rewrite `app/index.html`. Every id in the contract preserved.
14. Rewrite `app/src/styles.css`.
15. `npm run build`, then the e2e suite. Mutation-check `.page-chip`.
16. `hindi-check.mjs` for non-Latin output legibility.
17. Look at light, dark, 900px, and 200% zoom.

## Estimate

Phase A is half a day; step 6's retune is the only part likely to loop. Phase B is
a day.

**What is hard to reverse:** only step 11. npm versions cannot be unpublished
after 72 hours, and the unscoped name `achroma` is a one-time claim. Everything
else is a new private repo or a stylesheet.

The stylesheet rewrite is the largest diff and touches no logic — `main.ts`,
`pages.ts` and `ocr.worker.ts` are untouched, so the OCR path cannot regress. The
two places where care is warranted are the font fallback chain for non-Latin
output, and `.page-chip`, which is the one contract element that would fail
quietly.
