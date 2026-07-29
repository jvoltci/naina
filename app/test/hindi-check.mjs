// Does the deployed app actually read Hindi once the script selector is set?
import { chromium } from 'playwright';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const DIST = resolve(dirname(fileURLToPath(import.meta.url)), '..', 'dist');
const PORT = 4181;
const MIME = { '.html':'text/html', '.js':'text/javascript', '.mjs':'text/javascript',
  '.css':'text/css', '.wasm':'application/wasm', '.json':'application/json',
  '.webmanifest':'application/manifest+json', '.svg':'image/svg+xml',
  '.onnx':'application/octet-stream', '.yml':'text/yaml', '.map':'application/json' };
const server = createServer(async (req,res) => {
  const u = new URL(req.url, `http://localhost:${PORT}`);
  let f = resolve(DIST, `.${decodeURIComponent(u.pathname)}`);
  if (u.pathname.endsWith('/')) f = resolve(f, 'index.html');
  try {
    const b = await readFile(f);
    res.writeHead(200, {'content-type': MIME[f.slice(f.lastIndexOf('.'))] ?? 'application/octet-stream'});
    res.end(b);
  } catch { res.writeHead(404).end('nf'); }
});
await new Promise(r => server.listen(PORT, r));

const browser = await chromium.launch({ channel: 'chrome' });
const page = await browser.newPage();
const errs = [];
page.on('pageerror', e => errs.push(e.message));
page.on('console', m => { if (m.type()==='error') errs.push(m.text()); });

await page.goto(`http://localhost:${PORT}/`, { waitUntil: 'load' });
await page.selectOption('#language', 'devanagari');
console.log('script selector set to devanagari');
await page.setInputFiles('#file', process.argv[2]);
await page.waitForFunction(
  () => /mean confidence \d/.test(document.getElementById('meta')?.textContent ?? ''),
  undefined, { timeout: 300000 });

const meta = await page.locator('#meta').textContent();
const out  = await page.locator('#output').textContent();
console.log('meta:', meta.trim());
const lines = out.split('\n').filter(l => l.trim()).slice(0, 4);
console.log('first lines:');
for (const l of lines) console.log('  ', l.slice(0, 70));

// Devanagari is U+0900..U+097F. Assert real Devanagari came back.
const dev = (out.match(/[ऀ-ॿ]/g) ?? []).length;
console.log(`devanagari characters in output: ${dev}`);
const BENIGN = /webgpu|VerifyEachNode|Some nodes|Rerunning|favicon|CleanUnusedInitializers|Removing initializer/i;
const real = errs.filter(m => !BENIGN.test(m));
console.log('console errors:', real.length ? real.slice(0,2) : 'none');
await browser.close(); server.close();
process.exit(dev > 100 && real.length === 0 ? 0 : 1);
