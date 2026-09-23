# The Altrusian design system

Locked by Jai 2026-09-24. The spec and the prior-art research behind it are in
the lab's private notes, dated the same day.

**nilam is the system. Altrusian is its house theme.** Material is Google's
system and every Google product is a theme of it; the same shape here. Every
surface Jai ships (altrusian.com, naina, sparsh, jvoltci.github.io and The Log)
wears this theme.

## The files, copied whole into every surface, changed in all or none

| File | What it is |
|---|---|
| `altrusian.tokens.css` | nilam 2.0.0 output of `npx nilam '#1634C2'`: hue 265.9, light step 9 pinned to #1634C2 |
| `altrusian.css` | the font stacks, the page rule, the theme switch style |
| `theme.js` | the dark and light switch: a pre-paint line for `<head>` and a button wirer |

Load order: nilam's layers, then `altrusian.tokens.css`, then `altrusian.css`.

## The tiers, in Material's words

| Material tier | Material example | Here |
|---|---|---|
| reference | `md.ref.palette.primary40` | the solved steps `--brand-1..12`, `--neutral-1..12`, and #1634C2 |
| system | `md.sys.color.primary`, `md.sys.typescale.body-large` | `--brand-9`, `--neutral-12`, `--text-1`, `--space-4`, `--r-2`, `--shadow-1`, `--dur-1` |
| component | `md.comp.filled-button.container.color` | `.n-btn`, `.n-card`, `.n-note`, `.n-field` |

## Colour

- One blue. In light, `--brand-9` is #1634C2 itself: 9.1:1 on white, white ink
  9.1:1. In dark it is the glow, #628cf7, beside the family disc #618FFF.
- The pin removes nilam's light tritanopia brand/ok collapse (0.0773 to 0.1997,
  floor 0.09). The dark glow still collapses there, as every blue does, so ok
  states always carry a glyph or a word, never colour alone.
- The page is `--neutral-1` (#f6f8fd, #0f1014). Cards are `--surface`.
- Flat. Never a gradient.

## Type

One face: Inter, self-hosted from `@fontsource-variable/inter`, latin 48.3 kB.
Display is the same face, larger and tighter. Code is JetBrains Mono, only where
code is shown. Sizes are nilam's `--text-000..7`; body is `--text-1`.

## Shape, space, motion, depth

nilam's scale, nothing added: `--space-0..9` on a 4 px base, `--r-1..6` and
`--r-full`, `--dur-0..3` (80, 150, 240, 400 ms) with `--ease-out`,
`--shadow-1..3`, `.n-container` 72rem. No px or Tailwind literals for these.

## Icons

The galaxy and the transit marks are generated, never redrawn. Interface icons
are Lucide only: 24 grid, stroke 2, round caps, `currentColor`, ISC.

## Dark mode

Both modes on every surface. The OS decides until the reader chooses; the
choice is `.light` or `.dark` on `<html>`, remembered, from `theme.js`.

One recorded exception: the Sparsh app is dark only. It is a transfer and
video tool, the video stage is the brightest thing on screen, and a light page
around it reads as a bug (the reasoning is in sparsh/frontend/src/app/globals.css).
Its docs site has both modes.

## The first glance

1. One chromatic thing per first screen.
2. One face.
3. Space before lines: separate with space first, a `--neutral-6` hairline
   second, a shadow last.
4. Nothing moves unless it answers an action; reduced motion is honoured.
5. Both modes, screenshot both, desktop and phone.
6. No em or en dashes in anything a reader sees. Grep before shipping.

## The tool page (locked 2026-09-24)

Every tool Jai ships (naina, Sparsh, and whatever comes next) opens the same
way, and the pattern is locked: "love it, lock it, this design for any
presentation tool like naina, sparsh, etc., we will follow this style in
future too."

1. **The first screen is the one job.** One matte card for the thing the
   tool does (naina: a drop zone; Sparsh: the devices and a drop zone). No
   headline painted, no tagline painted, no explanation above the fold. The
   headline and the sentence stay in the page as `n-sr-only` for search
   engines and screen readers.
2. **One quiet line holds the options.** Under the card, one line in
   `--neutral-11` says what is set ("Tiny · Latin, Chinese, Japanese",
   "Add a device"). It opens to every control and every note. Nothing is
   removed from the product; everything is one click away.
3. **Matte.** The card is `--surface` with a `--neutral-6` hairline and
   `--r-5`. No gradient, no rim, no dashed brand border, no shadow at rest.
   Hover and drag may move the border and fill to the brand steps.
4. **One chromatic thing.** The filled `--brand-9` disc, or the online
   device's disc. Eyebrows and labels are neutral.
5. **Plain words, few of them.** Short sentences. Say what it does, not how
   clever it is. No em dashes anywhere a reader can see; the build fails on
   one (`check-dashes.mjs`).
6. **The explanation goes below the fold**, short, four notes at most.

Rendered and locked on the real builds: naina 9792129, Sparsh 75870c7,
sheets in the lab's private notes (design audit, 2026-09-24).

## Hardening

- `check-dashes.mjs` runs after every build on the built HTML and JS
  (naina `npm run build`, Sparsh `npm run build`). A dash a reader could see
  fails the build.
- naina's `scripts/first-glance.mjs` asserts the hero is `n-sr-only` and the
  options `<details>` exists in the built page.
- Sparsh's AirDrop-flow e2e asserts the first screen shows the tiles and the
  drop zone and hides the connect panel until "Add a device" is tapped.

## Changing the theme

Re-run `npx nilam '#1634C2' --css=...`, replace the nilam header comment with
the one in `altrusian.tokens.css`, copy the three files to every surface, and
screenshot all of them in both modes before any of them ship.
