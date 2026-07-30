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
| Where it lives | Standalone versioned package in this repo: `achroma/`. |
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

```css
/* light — canonical */
--n-0:    oklch(1     0 0);   /* reserved; card tops, canvas         */
--n-25:   oklch(0.985 0 0);   /* paper — page background             */
--n-50:   oklch(0.968 0 0);   /* subtle fill                         */
--n-100:  oklch(0.945 0 0);   /* raised surface                      */
--n-150:  oklch(0.922 0 0);   /* hairline                            */
--n-200:  oklch(0.900 0 0);   /* rule                                */
--n-300:  oklch(0.840 0 0);   /* strong border                       */
--n-400:  oklch(0.720 0 0);   /* disabled                            */
--n-500:  oklch(0.620 0 0);   /* faint — non-text and large text only */
--n-600:  oklch(0.520 0 0);   /* secondary text                      */
--n-700:  oklch(0.400 0 0);
--n-800:  oklch(0.300 0 0);
--n-900:  oklch(0.220 0 0);
--n-950:  oklch(0.160 0 0);   /* ink — body text                     */
--n-1000: oklch(0.090 0 0);
```

Dark mode is **not** a naive inversion: the range is compressed and the black is
lifted, because inverted hairlines at full contrast glare.

```css
/* dark — derived */
--bg:        oklch(0.170 0 0);
--bg-raised: oklch(0.215 0 0);
--hairline:  oklch(0.280 0 0);
--fg:        oklch(0.960 0 0);
--fg-dim:    oklch(0.720 0 0);
```

### Semantic hues — the only colour

Low chroma on purpose, so they read as signal rather than decoration.

| Token | Light | Dark |
|---|---|---|
| `--danger` | `oklch(0.55 0.19 27)` | `oklch(0.70 0.17 25)` |
| `--warn` | `oklch(0.62 0.13 75)` | `oklch(0.78 0.13 80)` |
| `--ok` | `oklch(0.55 0.12 150)` | `oklch(0.72 0.14 155)` |

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

```
achroma/
  package.json          @jvoltci/achroma
  achroma.css           tokens, both modes, base reset, .grain, .label
  achroma.tailwind.css  @theme inline bridge → shadcn's variable names
  proof.html            every token rendered; the visual test surface
  test/contrast.mjs     asserts the ramp (see Verification)
  README.md
```

Two consumers, two unrelated stacks, so the source of truth is plain CSS
variables — the only thing both eat natively.

- **naina/app** — Vite, plain CSS. Adds `"@jvoltci/achroma": "file:../achroma"`,
  matching the existing `file:../bindings/wasm` precedent, and imports
  `achroma.css` ahead of `styles.css`.
- **tool** — Next 15, Tailwind v4, shadcn. Imports `achroma.css` plus the bridge,
  which remaps `--primary`, `--border`, `--ring`, `--card` and the rest to Achroma
  values. Radix components inherit with no edits to their files. Cycle 3.

`proof.html` exists because a token file cannot be reviewed by reading it. It
renders the full ramp, every type step, both modes side by side, the grain on and
off, and each component pattern.

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

| Pair | Target |
|---|---|
| `--fg` on `--bg` | ≥ 7:1 (AAA body) |
| `--fg-dim` on `--bg` | ≥ 4.5:1 (AA body) |
| `--fg-faint` on `--bg` | ≥ 3:1 — and it is documented as non-text/large-text only |
| `--hairline` on `--bg` | ≥ 1.4:1, so rules are visible without glaring |
| each semantic on `--bg` | ≥ 4.5:1 |

Ratios are **not** stated in this spec, because they have not been measured yet.
The script computes them; whatever fails gets retuned before ship.

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

## Steps

1. `achroma/` scaffold: `package.json`, `README.md`.
2. `achroma.css` — ramp, semantics, type, space, radius, motion, both modes,
   `.grain`, `.label`, and a base layer that is deliberately small: `box-sizing`,
   margin reset, `-webkit-text-size-adjust`, `:focus-visible` ring, and
   `prefers-reduced-motion` zeroing every duration. Nothing else — an opinionated
   reset in a shared package fights each consumer's own base styles.
3. Self-host Geist and Geist Mono variable fonts. No Google Fonts request: the app
   claims to work offline, and a webfont fetch would break that claim quietly
   rather than loudly. Confirm the licence is OFL-1.1 before vendoring — this
   project ships Apache-2.0 models on purpose and should not acquire a font with
   unclear terms by accident.
4. `test/contrast.mjs` — chroma assertion first, then the contrast table. Retune
   the ramp against real numbers.
5. `proof.html`.
6. `achroma.tailwind.css` bridge. Written now, unused until cycle 3.
7. naina: wire the dependency, rewrite `index.html`, rewrite `styles.css`.
8. Run the app suites. Mutation-check `.page-chip`.
9. Look at all four states: light, dark, 900px, 200% zoom.

## Estimate

Steps 1–6 are half a day; the ramp retune in step 4 is the only part likely to
loop. Steps 7–9 are a day.

Nothing here is hard to reverse. The stylesheet rewrite is the largest diff and
touches no logic; `main.ts` and the worker are untouched, so the OCR path cannot
regress. Step 3 is the one worth care — a webfont served from a CDN would break
the offline guarantee silently, which is the failure shape this project keeps
getting bitten by.
