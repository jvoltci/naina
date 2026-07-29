// End-to-end check in a real browser.
//
// Everything else in this repo is tested off-browser, which cannot exercise the
// parts most likely to break: OffscreenCanvas, createImageBitmap, the module
// worker, pdf.js rasterisation, and Emscripten's ASYNCIFY inside a worker.
//
// Run:  node test/e2e.mjs            (needs a built dist/ and Google Chrome)
//       NAINA_E2E_PDF=/path.pdf node test/e2e.mjs
//
// Uses the installed Chrome via channel rather than Playwright's own build, so
// no 150 MB browser download is needed.

import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

const HERE = dirname(fileURLToPath(import.meta.url));
const DIST = resolve(HERE, '..', 'dist');
const PORT = 4179;

let failures = 0;
const ok = (cond, what) => {
  if (cond) console.log(`  ok   ${what}`);
  else {
    console.error(`  FAIL ${what}`);
    failures++;
  }
};
const skip = (why) => {
  console.log(`SKIP e2e: ${why}`);
  process.exit(0);
};

if (!existsSync(resolve(DIST, 'index.html'))) skip('dist/ not built — run npm run build');

const imagePath = process.env.NAINA_E2E_IMAGE;
const pdfPath = process.env.NAINA_E2E_PDF;
if (!imagePath && !pdfPath) skip('set NAINA_E2E_IMAGE and/or NAINA_E2E_PDF');

let chromium;
try {
  ({ chromium } = await import('playwright'));
} catch {
  skip('playwright not installed — npm i -D playwright');
}

// Static server over dist/. `npx serve` would add a dependency; Node can do it.
const { createServer } = await import('node:http');
const { readFile } = await import('node:fs/promises');
const MIME = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
  '.css': 'text/css',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.webmanifest': 'application/manifest+json',
  '.svg': 'image/svg+xml',
  '.map': 'application/json',
};
const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://localhost:${PORT}`);
  let file = resolve(DIST, `.${decodeURIComponent(url.pathname)}`);
  if (url.pathname === '/' || url.pathname.endsWith('/')) file = resolve(file, 'index.html');
  try {
    const body = await readFile(file);
    const ext = file.slice(file.lastIndexOf('.'));
    res.writeHead(200, { 'content-type': MIME[ext] ?? 'application/octet-stream' });
    res.end(body);
  } catch {
    res.writeHead(404).end('not found');
  }
});
await new Promise((r) => server.listen(PORT, r));

const browser = await chromium.launch({ channel: 'chrome' });
const page = await browser.newPage();

// Surface page errors: a silent exception in the worker would otherwise look
// like a timeout and send us debugging the wrong thing.
const pageErrors = [];
page.on('pageerror', (e) => pageErrors.push(e.message));
page.on('console', (m) => {
  if (m.type() === 'error') pageErrors.push(m.text());
});

try {
  await page.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
  ok(await page.title() !== '', 'page loads');
  ok((await page.locator('#drop').count()) === 1, 'drop zone renders');

  for (const [label, file] of [
    ['image', imagePath],
    ['pdf', pdfPath],
  ]) {
    if (!file || !existsSync(file)) continue;
    console.log(`\n${label}: ${file}`);

    // Reload between files. Without it, #results is already visible from the
    // previous file and every wait below resolves instantly against stale
    // content — which is exactly how this test first lied to me.
    await page.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
    await page.setInputFiles('#file', file);

    // First run downloads ~11 MB of weights plus ort-web's 27 MB wasm, then
    // does real inference. Generous, and it fails with the reason rather than a
    // bare timeout.
    try {
      await page.waitForSelector('#results:not([hidden])', { timeout: 240_000 });
    } catch (e) {
      const status = await page.locator('#status-text').textContent().catch(() => '');
      const err = await page.locator('#error').textContent().catch(() => '');
      console.error(`  status: ${status}`);
      console.error(`  error:  ${err}`);
      throw e;
    }

    // #results appearing only means the first page painted. Wait for the meta
    // line to carry a real measurement before reading anything.
    await page.waitForFunction(
      () => /mean confidence \d/.test(document.getElementById('meta')?.textContent ?? ''),
      undefined,
      { timeout: 240_000 },
    );

    const output = (await page.locator('#output').textContent()) ?? '';
    const meta = (await page.locator('#meta').textContent()) ?? '';
    console.log(`  meta: ${meta.trim()}`);
    console.log(`  first line: ${output.split('\n')[0].slice(0, 72)}`);

    ok(output.length > 200, `${label}: produced substantial text`);
    ok(
      output.includes('Deterministic Optical Character Recognition'),
      `${label}: read the page title`,
    );
    ok(/mean confidence 0\.9\d/.test(meta), `${label}: mean confidence above 0.9`);

    // The canvas overlay must actually have been painted.
    const painted = await page.evaluate(() => {
      const c = document.getElementById('canvas');
      return c instanceof HTMLCanvasElement && c.width > 0 && c.height > 0;
    });
    ok(painted, `${label}: page canvas rendered`);

    if (label === 'pdf') {
      // Pages are read one at a time and painted as they finish, so the second
      // chip appears strictly after the first result.
      await page
        .waitForFunction(() => document.querySelectorAll('#pager .page-chip').length === 2, undefined, {
          timeout: 240_000,
        })
        .catch(() => {});
      const chips = await page.locator('#pager .page-chip').count();
      ok(chips === 2, `pdf: pager shows both pages (got ${chips})`);
      ok(
        (await page.locator('#download-all').isVisible()),
        'pdf: "Save all" appears for multi-page',
      );

      // Second page must be reachable and hold its own, different text.
      await page.locator('#pager .page-chip').nth(1).click();
      const second = (await page.locator('#output').textContent()) ?? '';
      ok(second !== output, 'pdf: second page has different content');
      ok(second.includes('Annual Report'), 'pdf: second page read correctly');
    }

    // Plain-text view must drop markdown syntax.
    await page.locator('#tab-txt').click();
    const txt = (await page.locator('#output').textContent()) ?? '';
    ok(!txt.includes('# '), `${label}: text view has no markdown headings`);
    await page.locator('#tab-md').click();
  }

  // ONNX Runtime logs several things at warning level that are purely
  // informational: which execution provider it settled on, and that shape ops
  // stay on CPU by design. Filtering them by pattern keeps the assertion useful
  // for real errors instead of permanently red.
  const BENIGN =
    /webgpu|falling back|VerifyEachNodeIsAssignedToAnEp|Some nodes were not assigned|Rerunning with verbose|favicon/i;
  const realErrors = pageErrors.filter((m) => !BENIGN.test(m));
  ok(realErrors.length === 0, `no console errors (${realErrors.slice(0, 2).join(' | ')})`);
} finally {
  await browser.close();
  server.close();
}

if (failures) {
  console.error(`\n${failures} failure(s)`);
  process.exit(1);
}
console.log('\ne2e: all passed');
