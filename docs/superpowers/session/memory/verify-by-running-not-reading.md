---
name: verify-by-running-not-reading
description: "On this user's work, verify claims by executing them — written plans and agent reports here have repeatedly contained confident errors that only running caught"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 484389e7-4ff9-49a3-bfd5-d8f73e5fe0e7
  modified: 2026-07-29T05:57:46.738Z
---

Do not treat a written plan, a hand-computed number, or a subagent's report as
verified. On the naina work every single one of these was wrong and was caught
only by execution:

- Two hand-computed test assertions in my own implementation plan (a rounding
  mode, and two polygon areas off by a factor of 2). A subagent correctly fixed
  the *assertion* rather than bending working code to match it.
- "All four PP-DocLayout variants emit `[N,6]`" — V3 emits `[N,7]` with 25
  classes and a 48 MB mask tensor. Medium tier failed outright.
- "The MCP server targets spec 2026-07-28" — the newest published SDK caps at
  `2025-11-25` and negotiates down. Only measuring showed this.
- A clean `rm -rf build` silently dropped the only inference backend
  (`NAINA_WITH_ONNXRUNTIME` defaults OFF) and the suite still reported 100%
  green, because every test either needs no backend or *skips* without one.

**Why:** a green test suite proves nothing if the tests skip, and a plan's
arithmetic is just an assertion until something executes it.

**How to apply:** run the thing. Prefer mutation-testing a load-bearing claim
(break it deliberately, confirm a test fails) over asserting the design is
right. Make skips loud — a silent skip is indistinguishable from a pass. When a
test expectation contradicts an implementation, work out which matches upstream
rather than relaxing the tolerance until it passes.

The user explicitly values this: they asked "what do you think honestly?" and
responded well to a list of gaps, not reassurance. State what does not work as
plainly as what does. See [[naina-ocr-pivot]].
