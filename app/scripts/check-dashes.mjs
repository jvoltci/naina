#!/usr/bin/env node
/* The dash guard. Copied whole with the other design/ files; changed in all or none.
 *
 *   node scripts/check-dashes.mjs dist/index.html dist/assets/*.js
 *
 * Fails the build when an em dash, or an en dash that is not between two digits,
 * appears in any file given. Run it on the BUILT output, not the source: the
 * shipped HTML and JS hold every string a reader can see and no code comments,
 * so a comment written years ago cannot fail the build and a string a reader
 * sees cannot pass it. Rule and reason: altrusian/design/DESIGN.md. */
import { readFileSync } from "node:fs";

const files = process.argv.slice(2);
if (files.length === 0) { console.error("check-dashes: give it the built files to scan"); process.exit(2); }
let bad = 0;
for (const f of files) {
  const lines = readFileSync(f, "utf8").split("\n");
  lines.forEach((line, i) => {
    if (line.includes("\u2014") || /(?<!\d)\u2013|\u2013(?!\d)/.test(line)) {
      bad++;
      console.error(`${f}:${i + 1}: ${line.trim().slice(0, 120)}`);
    }
  });
}
if (bad) {
  console.error(`\n${bad} line(s) carry an em dash or a stray en dash. Use a comma, a colon or a full stop.`);
  process.exit(1);
}
console.log(`check-dashes: 0 in ${files.length} file(s)`);
