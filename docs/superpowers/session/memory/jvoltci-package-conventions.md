---
name: jvoltci-package-conventions
description: "User is jvoltci on GitHub and ships real open-source packages; they have a consistent house style for READMEs, docs sites and repo layout that new projects should match"
metadata: 
  node_type: memory
  type: user
  originSessionId: 484389e7-4ff9-49a3-bfd5-d8f73e5fe0e7
  modified: 2026-07-29T06:19:14.005Z
---

GitHub account is **`jvoltci`** (`gh` is already authenticated). They publish
genuine packages, not experiments, and reuse one house style across them:

- `saf` — Flutter plugin, pub.dev v2.1.0, Kotlin under `com.jvoltci.saf`,
  `plugin_platform_interface`
- `breccia` — PyPI, block-scaled tensor primitives
- `naina` — PyPI (name already owned), the OCR runtime

**House style, worth copying by default on anything new:**

- README opens with an animated hero SVG at `docs/assets/hero.svg`
  (`viewBox="0 0 850 380"`, Outfit + JetBrains Mono via `@import`, CSS keyframes),
  then a badge row, a one-line centred `<h3>` tagline, a link row, then a code
  block with no preamble. Sections: Why → What you get → Benchmarks (each with a
  `Reproduce:` command) → Status table with ✅ → Install → Documentation → The name.
- Docs are mkdocs-material at `jvoltci.github.io/<project>/`, with
  `docs/assets/{hero,logo,favicon}.svg` and `extra.css`.
- Apache-2.0.

**Preferences established while working:**

- Wants things **platform-agnostic** by default — called this out unprompted.
  No hardcoded macOS paths, presets and CI for Linux/Windows too.
- Prefers **vendoring third-party assets into their own GitHub releases** rather
  than depending on upstream hosting, for durability. Asked for this directly.
- Do **not** ask them to paste tokens into chat. Use PyPI Trusted Publishing
  (OIDC) and repo secrets instead.
- Ambition is explicit: wants to lead the field, maintain long-term, and be
  genuinely best — so flag over-promising in their own docs when you see it.
- **Asked directly for plain language, permanently** ("Always response in easy
  plain lang so I can get everything easily from here on"). Keep status updates
  short and scannable, lead with whether a thing works or not, skip the jargon
  unless they ask for depth. They also ask for a tracker/table view of progress.

See [[naina-ocr-pivot]] and [[verify-by-running-not-reading]].
