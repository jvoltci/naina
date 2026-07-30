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
await page.waitForTimeout(2500);   // let the webfont land
await page.screenshot({ path: 'public/og.png', clip: { x: 0, y: 0, width: 1200, height: 630 } });
console.log('wrote public/og.png');
await browser.close();
