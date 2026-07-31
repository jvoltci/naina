// Render the OG card to PNG. Twitter and most unfurlers will not render SVG, and
// GitHub raw serves .svg as text/plain anyway, so the shipped card is a PNG.
import { chromium } from 'playwright';
import { readFileSync } from 'node:fs';

const browser = await chromium.launch({ channel: 'chrome' });
const page = await browser.newPage({ viewport: { width: 1200, height: 630 }, deviceScaleFactor: 2 });
await page.setContent(
  `<!doctype html><html><body style="margin:0">${readFileSync('assets/og-source.svg', 'utf8')}</body></html>`,
  { waitUntil: 'load' },
);
// The card is on the system font stack now, not a Google Fonts @import, so there
// is no webfont to wait for and nothing to be racing. 300ms for layout only; the
// 2500ms sleep this replaces was load-bearing and silently so — if it had ever
// been too short the card would have rendered in Helvetica and said nothing.
await page.waitForTimeout(300);
// The document must be dark, or the SVG's own --neutral-1 rect paints over a
// white page and the 1px gutter outside the viewBox shows through as a light rim.
await page.emulateMedia({ colorScheme: 'dark' });
await page.screenshot({ path: 'public/og.png', clip: { x: 0, y: 0, width: 1200, height: 630 } });
console.log('wrote public/og.png');
await browser.close();
