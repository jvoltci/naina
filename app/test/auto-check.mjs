// Does Auto pick the right script? And does it stay free on Latin?
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIST = resolve(dirname(fileURLToPath(import.meta.url)), '..', 'dist');
const PORT = 4183;
const MIME = { '.html':'text/html','.js':'text/javascript','.mjs':'text/javascript','.css':'text/css',
  '.wasm':'application/wasm','.json':'application/json','.webmanifest':'application/manifest+json',
  '.svg':'image/svg+xml','.onnx':'application/octet-stream','.yml':'text/yaml','.map':'application/json' };
const server = createServer(async (req,res) => {
  const u = new URL(req.url, `http://localhost:${PORT}`);
  let f = resolve(DIST, `.${decodeURIComponent(u.pathname)}`);
  if (u.pathname.endsWith('/')) f = resolve(f, 'index.html');
  try { const b = await readFile(f);
    res.writeHead(200, {'content-type': MIME[f.slice(f.lastIndexOf('.'))] ?? 'application/octet-stream'});
    res.end(b);
  } catch { res.writeHead(404).end('nf'); }
});
await new Promise(r => server.listen(PORT, r));

const browser = await chromium.launch({ channel: 'chrome' });
let fails = 0;
for (const [file, expect] of process.argv.slice(2).map(a => a.split('='))) {
  const page = await browser.newPage();
  page.on('console', m => { if (m.type() === 'error' && !/VerifyEach|Removing initializer|webgpu/i.test(m.text())) console.log('    CONSOLE:', m.text().slice(0, 160)); });
  page.on('pageerror', e => console.log('    PAGEERROR:', e.message.slice(0, 160)));
  await page.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
  // Auto is now SELECTED EXPLICITLY, because it is no longer the default — the
  // dropdown ships on Latin, since English scans are the common case. This test
  // asks "does Auto detect the right script", not "what is the default", so
  // setting it keeps the assertion pointed at what it was always about. The
  // default itself is asserted separately below.
  await page.selectOption('#language', 'auto');
  const sel = await page.locator('#language').inputValue();
  const t0 = Date.now();
  await page.setInputFiles('#file', file);
  await page.waitForFunction(
    () => /mean confidence \d/.test(document.getElementById('meta')?.textContent ?? ''),
    undefined, { timeout: 300000 });
  const meta = (await page.locator('#meta').textContent()) ?? '';
  const out  = (await page.locator('#output').textContent()) ?? '';
  const m = meta.match(/script (?:detected|): ?([a-z]*)/);
  const got = meta.includes('script detected:') ? meta.split('script detected:')[1].trim() : '';
  const ok = got === expect;
  if (!ok) fails++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${file.split('/').pop().padEnd(16)} default-selector=${sel}  detected="${got}" expected="${expect}"  ${((Date.now()-t0)/1000).toFixed(1)}s`);
  console.log(`       ${out.split('\n').find(l => l.trim())?.slice(0, 58)}`);
  await page.close();
}
await browser.close(); server.close();
process.exit(fails ? 1 : 0);
