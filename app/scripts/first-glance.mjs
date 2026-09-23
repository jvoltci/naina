#!/usr/bin/env node
/* The first-glance guard for naina's built page (altrusian/design/DESIGN.md,
 * "The tool page", locked 2026-09-24). Fails the build if the hero is painted
 * again or the options line is gone. Run after vite build. */
import { readFileSync } from "node:fs";

const html = readFileSync(new URL("../dist/index.html", import.meta.url), "utf8");
const checks = [
  ['the hero is read, not painted (class="hero n-sr-only")', /class="hero n-sr-only"/.test(html)],
  ["the options line exists (<details id=\"opts\">)", /<details class="opts" id="opts">/.test(html)],
  ['the blue eyebrow is gone (no <p class="label"> inside the hero)', !/<section class="hero[^"]*">\s*<p class="label"/.test(html)],
  ['the drop zone says "Drop a document"', html.includes("Drop a document")],
];
let failed = 0;
for (const [name, ok] of checks) { console.log(`${ok ? "ok  " : "FAIL"} ${name}`); if (!ok) failed++; }
if (failed) process.exit(1);
