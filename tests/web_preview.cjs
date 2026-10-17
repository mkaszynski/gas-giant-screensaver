// Serve the compiled preview under a project subpath, without isolation headers.
// The same check can target the deployed Pages URL with PREVIEW_URL.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('../web/node_modules/playwright');
const { PNG } = require('../web/node_modules/pngjs');
const root = path.resolve(__dirname, '../build-web');
const captureDir = path.resolve(__dirname, '../captures');
const mime = { '.js': 'text/javascript', '.css': 'text/css', '.html': 'text/html', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };
const errors = [];
let browser, server;
function imageStats(buffer) {
  const image = PNG.sync.read(buffer);
  let min = 255, max = 0, sum = 0;
  for (let y = image.height / 4 | 0; y < image.height * .7; y += 4) {
    for (let x = image.width / 4 | 0; x < image.width * .75; x += 4) {
      const i = (y * image.width + x) * 4;
      const light = (image.data[i] + image.data[i + 1] + image.data[i + 2]) / 3;
      min = Math.min(min, light); max = Math.max(max, light); sum += light;
    }
  }
  assert(max > 30 && max - min > 20, 'Rendered scene must contain visible detail');
  return sum;
}
async function ready(page, frames = 1) {
  await page.waitForFunction(n => window.previewMetrics?.frames >= n || document.querySelector('h2')?.textContent === 'Preview unavailable', frames, { timeout: 240000 });
  const failure = await page.locator('h2').textContent();
  assert.notEqual(failure, 'Preview unavailable', await page.locator('#status').textContent());
}
(async () => {
  fs.mkdirSync(captureDir, { recursive: true });
  let url = process.env.PREVIEW_URL;
  if (!url) {
    server = http.createServer((req, res) => {
      const pathname = new URL(req.url, 'http://localhost').pathname;
      if (!pathname.startsWith('/gas-giant-screensaver/')) { res.writeHead(404).end(); return; }
      const relative = decodeURIComponent(pathname.slice('/gas-giant-screensaver/'.length)) || 'index.html';
      const file = path.resolve(root, relative);
      if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) { res.writeHead(404).end(); return; }
      res.writeHead(200, { 'Content-Type': mime[path.extname(file)] || 'text/plain' });
      fs.createReadStream(file).pipe(res);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    url = `http://127.0.0.1:${server.address().port}/gas-giant-screensaver/`;
  }
  browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || undefined,
    headless: process.env.HEADED !== '1',
    args: ['--no-sandbox', '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader']
  });
  const context = await browser.newContext({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 2 });
  const page = await context.newPage();
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => {
    if (message.type() === 'error' && !message.text().includes('404')) errors.push(message.text());
  });
  // First navigation installs the static worker and reloads into isolation.
  await page.goto(url + '?paused=1&time=510&travel=162000&weather=1790900000');
  await page.getByRole('button', { name: 'Launch preview' }).click({ timeout: 60000 });
  // Inspect the production pixel budget, then keep CI software rendering small.
  assert.equal(await page.locator('#scene').getAttribute('width'), '1920');
  assert.equal(await page.locator('#scene').getAttribute('height'), '1080');
  async function smallCanvas() {
    await page.evaluate(() => {
      const select = document.querySelector('#resolution');
      select.add(new Option('CI diagnostic', '180'));
      select.value = '180';
      select.dispatchEvent(new Event('change'));
    });
  }
  await smallCanvas();
  await ready(page);
  assert(await page.evaluate(() => crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'));
  await page.waitForFunction(() => window.previewMetrics.width === 320 && window.previewMetrics.height === 180);
  // Transfer keeps the HTML canvas attributes; the worker owns bitmap size.
  const day = imageStats(await page.screenshot({ path: path.join(captureDir, 'web-day.png') }));
  const before = await page.evaluate(() => ({ ...window.previewMetrics, samples: undefined }));
  await page.waitForTimeout(250);
  assert.equal(await page.evaluate(() => window.previewMetrics.frames), before.frames, 'Pause must stop GPU redraws');
  assert.equal(await page.evaluate(() => window.previewMetrics.time), before.time, 'Paused timeline must stay fixed');
  await page.getByRole('button', { name: 'Resume', exact: true }).click();
  await page.waitForFunction(time => window.previewMetrics.time > time, before.time);
  await page.getByRole('button', { name: 'Pause', exact: true }).click();
  await page.waitForFunction(() => window.previewMetrics.paused, null, { timeout: 120000 });
  await page.evaluate(() => { const select = document.querySelector('#resolution'); select.add(new Option('CI resize', '270')); });
  await page.locator('#resolution').selectOption('270');
  await page.waitForFunction(() => window.previewMetrics.width === 480 && window.previewMetrics.height === 270);
  await page.locator('#time').fill('1200');
  await page.locator('#time').dispatchEvent('change');
  await page.waitForFunction(() => Math.abs(window.previewMetrics.time - 1200) < .01);
  const dusk = imageStats(await page.screenshot({ path: path.join(captureDir, 'web-dusk.png') }));
  assert(Math.abs(day - dusk) / day > .02, 'Seeking must visibly change the shared scene');
  // Force a terrain rebase and a worker build during moving flight.
  await page.goto(url + '?time=510&travel=162000&speed=1');
  await page.getByRole('button', { name: 'Launch preview' }).click();
  await smallCanvas();
  await ready(page);
  await page.waitForFunction(() => window.previewMetrics.samples.some(frame => frame.pending), null, { timeout: 240000 });
  const builds = await page.evaluate(() => window.previewMetrics.builds);
  await page.waitForFunction(n => window.previewMetrics.frames > n, await page.evaluate(() => window.previewMetrics.frames) + 3, { timeout: 120000 });
  assert(builds >= 2, 'Moving terrain must continue streaming in WASM');
  // A second visit uses the established service worker without another reload.
  await page.getByRole('button', { name: 'Stop preview' }).click();
  await page.getByRole('button', { name: 'Launch preview' }).click();
  await smallCanvas();
  await ready(page);
  assert(await page.evaluate(() => crossOriginIsolated));
  assert.deepEqual(errors, []);
  fs.writeFileSync(path.join(captureDir, 'web-validation.json'), JSON.stringify({ url, day, dusk, builds, metrics: await page.evaluate(() => window.previewMetrics) }, null, 2));
  console.log('Static WASM preview: isolation, rendering, pause/resume, seek, HiDPI pixel budget, resize, terrain worker and stop/relaunch passed');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
  if (browser) await browser.close();
  if (server) await new Promise(resolve => server.close(resolve));
});
