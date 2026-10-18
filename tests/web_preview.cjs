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
const backend = process.env.PREVIEW_BACKEND || 'webgl2';
assert(['auto', 'webgpu', 'webgl2'].includes(backend));
const expectedBackend = backend === 'auto' ? 'webgl2' : backend;
function address(url, query) {
  const target = new URL(url);
  target.search = query;
  target.searchParams.set('backend', backend);
  return target.href;
}
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
    args: ['--no-sandbox', '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader', '--use-angle=swiftshader',
      ...(backend === 'auto' ? ['--disable-webgpu'] : ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader'])]
  });
  const context = await browser.newContext({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 2 });
  const page = await context.newPage();
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => {
    if (message.type() === 'error' && !message.text().includes('404')) errors.push(message.text());
  });
  // First navigation installs the static worker and reloads into isolation.
  await page.goto(address(url, 'paused=1&time=510&travel=162000&weather=1790900000'));
  await page.waitForFunction(() => !document.querySelector('#launch').disabled, null, {timeout: 60000});
  if (backend === 'webgpu') {
    const failures = await page.evaluate(async () => {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) return ['No WebGPU adapter'];
      const device = await adapter.requestDevice();
      const definitions = await (await fetch('webgpu-shaders.json')).json();
      const errors = [];
      for (const program of Object.values(definitions)) for (const stage of ['vert', 'frag']) {
        const info = await device.createShaderModule({code: program[stage].code}).getCompilationInfo();
        for (const item of info.messages) if (item.type === 'error') errors.push(program.label + ': ' + item.message);
      }
      device.destroy();
      return errors;
    });
    assert.deepEqual(failures, [], 'Every offline WGSL variant must pass browser validation');
  }
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
  assert.equal(await page.evaluate(() => window.previewMetrics.backend), expectedBackend);
  assert(await page.evaluate(() => crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined'));
  await page.waitForFunction(() => window.previewMetrics.width === 320 && window.previewMetrics.height === 180);
  // Transfer keeps the HTML canvas attributes; the worker owns bitmap size.
  const day = imageStats(await page.screenshot({ path: path.join(captureDir, `web-${backend}-day.png`) }));
  async function capturePixels(name) {
    const width = await page.evaluate(() => window.previewMetrics.width);
    const height = await page.evaluate(() => window.previewMetrics.height);
    const style = await page.addStyleTag({content: `header,footer,#loading{visibility:hidden}#scene{width:${width}px;height:${height}px;object-fit:fill}`});
    await page.locator('#scene').screenshot({path: path.join(captureDir, `web-${backend}-${name}-pixels.png`)});
    await style.evaluate(node => node.remove());
  }
  await capturePixels('day');
  if (backend === 'auto') {
    assert.equal(await page.locator('#backend-status').textContent(), 'WebGL 2');
    console.log('Automatic WebGL 2 fallback rendered successfully without WebGPU');
    return;
  }
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
  const dusk = imageStats(await page.screenshot({ path: path.join(captureDir, `web-${backend}-dusk.png`) }));
  await capturePixels('dusk');
  assert(Math.abs(day - dusk) / day > .02, 'Seeking must visibly change the shared scene');
  for (const fixture of [
    {name: 'night', time: 437625, weather: 1790900000, travel: 0, options: '&surface=0'},
    {name: 'night-no-stars', time: 437625, weather: 1790900000, travel: 0, options: '&surface=0&stars=0'},
    {name: 'storm', time: 510, weather: 1260, travel: 162000},
    {name: 'thin-clouds', time: 510, weather: 1790900000, travel: 162000, options: '&volumetric=0'}
  ]) {
    await page.goto(address(url, `paused=1&time=${fixture.time}&travel=${fixture.travel}&weather=${fixture.weather}${fixture.options || ''}`));
    await page.getByRole('button', {name: 'Launch preview'}).click();
    await smallCanvas();
    await ready(page);
    await capturePixels(fixture.name);
  }
  // Force a terrain rebase and a worker build during moving flight.
  await page.goto(address(url, 'time=510&travel=162000&speed=1'));
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
  fs.writeFileSync(path.join(captureDir, `web-${backend}-validation.json`), JSON.stringify({ url, day, dusk, builds, metrics: await page.evaluate(() => window.previewMetrics) }, null, 2));
  console.log(backend + ' static WASM preview: isolation, rendering, pause/resume, seek, HiDPI pixel budget, resize, terrain worker and stop/relaunch passed');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
  if (browser) await browser.close();
  if (server) await new Promise(resolve => server.close(resolve));
});
