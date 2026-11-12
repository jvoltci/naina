import { chromium } from 'playwright';
const URL_ = 'https://jvoltci.github.io/naina/';
const browser = await chromium.launch({ channel: 'chrome' });
const page = await browser.newPage();
const errs = [];
page.on('pageerror', e => errs.push(e.message));
page.on('console', m => { if (m.type() === 'error') errs.push(m.text()); });

console.log(`loading ${URL_}`);
await page.goto(URL_, { waitUntil: 'load' });
console.log('title:', await page.title());

await page.setInputFiles('#file', process.argv[2]);
await page.waitForFunction(
  () => /mean confidence \d/.test(document.getElementById('meta')?.textContent ?? ''),
  undefined, { timeout: 300000 });

const meta = await page.locator('#meta').textContent();
const out = await page.locator('#output').textContent();
console.log('meta:', meta.trim());
console.log('first 2 lines:');
console.log(out.split('\n').slice(0, 2).map(l => '  ' + l.slice(0, 76)).join('\n'));

const offline = await page.locator('#offline-badge').isVisible();
console.log('offline badge (service worker registered):', offline);

const BENIGN = /webgpu|VerifyEachNodeIsAssignedToAnEp|Some nodes|Rerunning with verbose|favicon/i;
const real = errs.filter(m => !BENIGN.test(m));
console.log('console errors:', real.length ? real.slice(0,3) : 'none');
await browser.close();
