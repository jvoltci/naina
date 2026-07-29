---
name: naina-known-gaps
description: "naina v0.2.1 shipped to all five registries; what remains open, and the platform facts that will bite when ONNX Runtime is bumped"
metadata: 
  node_type: memory
  type: project
  originSessionId: 484389e7-4ff9-49a3-bfd5-d8f73e5fe0e7
  modified: 2026-07-29T11:16:07.470Z
---

**v0.2.1 is published on all five registries** (2026-07-29): PyPI, npm
`@jvoltci/naina` + `@jvoltci/naina-wasm`, crates.io, pub.dev (160/160 points).
Web app and docs live at `jvoltci.github.io/naina`. Ten alphabets. Android
verified on a device.

**Open, in priority order:**

1. ~~naina cannot tell it was given the wrong script.~~ **Solved.**
   `language="auto"` in the core recognises a sample of detected boxes with each
   alphabet and keeps the best if it beats the default by 0.03. Measured margins:
   Hindi +0.426, Cyrillic +0.104, Greek +0.066, Latin +0.006 (where every
   alphabet ties near 0.98 because they all contain Latin — which is why a plain
   argmax is wrong). Costs 88 MB staged against 11 MB named, so the web app reads
   with the default first and only fetches the rest when a result comes back weak.
2. **iOS is unverified.** The podspec has never been built or run. Android is
   fine.
3. **npm tokens expire in 90 days**, so the release pipeline breaks on a timer.
   Both npm jobs already request `id-token: write`; switch to npm Trusted
   Publishing and delete `NODE_AUTH_TOKEN`.
4. MCP: not on npm, no PDF input, protocol capped at `2025-11-25` by the SDK.

**Platform floors, inherited not chosen — these move when ONNX Runtime is
bumped:** macOS **13.3+** (ORT's dylib minimum on *both* arm64 and x86_64),
Linux **glibc 2.28+** (manylinux_2_28), Android **API 28+** (`std::aligned_alloc`),
GCC needs `-Wno-restrict` (libstdc++ false positive at -O3).

**Traps worth remembering:**

- `clang-format` corrupts JavaScript inside `EM_JS` macros — rewrites `===` to
  `== =`. Compiles fine, fails much later as an acorn error in generated code.
  The region is fenced; do not unfence it.
- `naina.wasm` **embeds** `registry.yaml`, so registry changes need a WASM
  rebuild or staging silently serves the old list.
- GitHub **raw** serves `.svg` as `text/plain` with `nosniff`, so raw SVG URLs
  never render off-GitHub. GitHub **Pages** serves `image/svg+xml` correctly —
  use the Pages URL for READMEs.
- GitHub **release assets cannot be fetched from a browser** (302 to
  release-assets.githubusercontent.com, no CORS on either hop). The web app
  serves weights same-origin from its Pages artifact.
- WebGPU (ORT JSEP) **silently drops layout** — text stays at 0.99 while regions
  go 9 → 0. Off by default; do not re-enable without checking layout output.

See [[naina-ocr-pivot]] and [[verify-by-running-not-reading]].
