// Serve the compiled preview under a project subpath, without isolation headers.
// The same check can target the deployed Pages URL with PREVIEW_URL.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('../web/node_modules/playwright');
const { PNG } = require('../web/node_modules/pngjs');
const fixtures = require('./scene_fixtures.cjs');
const { difference, matches } = require('./web_native_parity.cjs');
const root = path.resolve(__dirname, '../build-web');
const captureDir = path.resolve(__dirname, '../captures');
const mime = {
  '.js': 'text/javascript',
  '.css': 'text/css',
  '.html': 'text/html',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
};
const errors = [];
const backend = process.env.PREVIEW_BACKEND || 'webgl2';
const uiOnly = process.env.PREVIEW_UI_ONLY === '1';
assert(['auto', 'webgpu', 'webgl2'].includes(backend));
const expectedBackend = backend === 'auto' ? 'webgl2' : backend;
function address(url, query) {
  const target = new URL(url);
  target.search = query;
  target.searchParams.set('backend', backend);
  if (!target.searchParams.has('quality'))
    target.searchParams.set('quality', '2');
  if (!target.searchParams.has('height'))
    target.searchParams.set('height', '180');
  if (uiOnly)
    for (const feature of ['surface', 'volumetric', 'galaxy', 'stars'])
      target.searchParams.set(feature, '0');
  return target.href;
}
let browser, server, page;
function imageStats(buffer) {
  const image = PNG.sync.read(buffer);
  let min = 255,
    max = 0,
    sum = 0;
  for (let y = (image.height / 4) | 0; y < image.height * 0.7; y += 4) {
    for (let x = (image.width / 4) | 0; x < image.width * 0.75; x += 4) {
      const i = (y * image.width + x) * 4;
      const light = (image.data[i] + image.data[i + 1] + image.data[i + 2]) / 3;
      min = Math.min(min, light);
      max = Math.max(max, light);
      sum += light;
    }
  }
  assert(
    max > 30 && max - min > 20,
    'Rendered scene must contain visible detail',
  );
  return sum;
}
async function ready(page, frames = 1) {
  await page.waitForFunction(
    (n) =>
      window.previewMetrics?.frames >= n ||
      document.querySelector('h2')?.textContent === 'Preview unavailable',
    frames,
    { timeout: 240000 },
  );
  const failure = await page.locator('h2').textContent();
  assert.notEqual(
    failure,
    'Preview unavailable',
    await page.locator('#status').textContent(),
  );
}
async function checkInitialFlightTime() {
  assert(
    await page.evaluate(
      () =>
        window.previewMetrics.samples[0].time >= 510 &&
        window.previewMetrics.samples[0].time < 570,
    ),
    'An unpaused flight must start at its requested scene time after runtime initialization',
  );
}
(async () => {
  fs.mkdirSync(captureDir, { recursive: true });
  let url = process.env.PREVIEW_URL;
  if (!url) {
    server = http.createServer((req, res) => {
      const pathname = new URL(req.url, 'http://localhost').pathname;
      if (!pathname.startsWith('/gas-giant-screensaver/')) {
        res.writeHead(404).end();
        return;
      }
      const relative =
        decodeURIComponent(pathname.slice('/gas-giant-screensaver/'.length)) ||
        'index.html';
      const file = path.resolve(root, relative);
      if (
        !file.startsWith(root + path.sep) ||
        !fs.existsSync(file) ||
        !fs.statSync(file).isFile()
      ) {
        res.writeHead(404).end();
        return;
      }
      res.writeHead(200, {
        'Content-Type': mime[path.extname(file)] || 'text/plain',
      });
      fs.createReadStream(file).pipe(res);
    });
    await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
    url = `http://127.0.0.1:${server.address().port}/gas-giant-screensaver/`;
  }
  browser = await chromium.launch({
    executablePath: process.env.CHROMIUM_PATH || undefined,
    headless: process.env.HEADED !== '1',
    args: [
      '--no-sandbox',
      '--ignore-gpu-blocklist',
      '--enable-unsafe-swiftshader',
      '--use-angle=swiftshader',
      ...(backend === 'auto'
        ? ['--disable-webgpu']
        : [
            '--enable-unsafe-webgpu',
            '--enable-features=Vulkan',
            '--use-vulkan=swiftshader',
          ]),
    ],
  });
  const context = await browser.newContext({
    viewport: { width: 1280, height: 720 },
    deviceScaleFactor: 2,
  });
  page = await context.newPage();
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error' && !message.text().includes('404'))
      errors.push(message.text());
  });
  // First navigation installs the static worker and reloads into isolation.
  await page.goto(
    address(url, 'paused=1&time=510&travel=162000&weather=1790900000&speed=0'),
  );
  await page.waitForFunction(
    () => !document.querySelector('#launch').disabled,
    null,
    { timeout: 60000 },
  );
  // Before launch these aspect/budget checks allocate no rendering context.
  for (const [width, height] of [
    [390, 844],
    [2400, 600],
    [1280, 720],
  ]) {
    await page.setViewportSize({ width, height });
    await page.waitForFunction(
      ({ width, height }) => {
        const canvas = document.querySelector('#scene');
        return (
          Math.abs(canvas.width / canvas.height - width / height) <
          2 / canvas.height
        );
      },
      { width, height },
    );
    const size = await page.evaluate(() => ({
      width: scene.width,
      height: scene.height,
      scroll: document.documentElement.scrollWidth,
      viewport: window.innerWidth,
    }));
    assert(
      size.width * size.height <= 1920 * 1080,
      'Resize must respect the HiDPI-independent pixel budget',
    );
    assert.equal(
      size.scroll,
      size.viewport,
      'The browser preview must not scroll',
    );
  }
  if (backend === 'webgpu' && !uiOnly) {
    const failures = await page.evaluate(async () => {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) return ['No WebGPU adapter'];
      const device = await adapter.requestDevice();
      const definitions = await (await fetch('webgpu-shaders.json')).json();
      const errors = [];
      for (const program of Object.values(definitions))
        for (const stage of ['vert', 'frag']) {
          const info = await device
            .createShaderModule({ code: program[stage].code })
            .getCompilationInfo();
          for (const item of info.messages)
            if (item.type === 'error')
              errors.push(program.label + ': ' + item.message);
        }
      device.destroy();
      return errors;
    });
    assert.deepEqual(
      failures,
      [],
      'Every offline WGSL variant must pass browser validation',
    );
  }
  await page
    .getByRole('button', { name: 'Launch preview' })
    .click({ timeout: 60000 });
  // Inspect the production pixel budget, then keep CI software rendering small.
  assert(
    Number(await page.locator('#scene').getAttribute('width')) <= 640,
    'Launch placeholder must remain small until C++ selects a budget',
  );
  async function smallCanvas(height = 180) {
    await page.evaluate((height) => {
      const select = document.querySelector('#resolution');
      select.add(new Option('CI diagnostic', String(height)));
      select.value = String(height);
      select.dispatchEvent(new Event('change'));
    }, height);
  }
  await smallCanvas();
  await ready(page);
  assert.equal(
    await page.evaluate(() => window.previewMetrics.backend),
    expectedBackend,
  );
  assert(
    await page.evaluate(
      () => crossOriginIsolated && typeof SharedArrayBuffer !== 'undefined',
    ),
  );
  await page.waitForFunction(
    () =>
      window.previewMetrics.width === 320 &&
      window.previewMetrics.height === 180,
  );
  // Transfer keeps the HTML canvas attributes; the worker owns bitmap size.
  assert(
    await page.locator('#debug-hint').isVisible(),
    'First launch must explain the corner access',
  );
  assert.match(await page.locator('#graphics-hint').textContent(),
    uiOnly ? /High quality · static landscape/ : /High quality · moving terrain/);
  assert(
    await page.locator('#browser-controls').isHidden(),
    'The flight starts without browser chrome',
  );
  const bounds = await page.locator('#scene').boundingBox();
  assert.equal(bounds.width, 1280);
  assert.equal(bounds.height, 720);
  const day = imageStats(
    await page.screenshot({
      path: path.join(captureDir, `web-${backend}-day.png`),
    }),
  );
  async function capturePixels(name) {
    const width = await page.evaluate(() => window.previewMetrics.width);
    const height = await page.evaluate(() => window.previewMetrics.height);
    const dpr = await page.evaluate(() => window.devicePixelRatio);
    // One framebuffer texel per physical screenshot pixel. CSS-size capture
    // at DPR 2 would otherwise filter the scene twice and hide real fidelity.
    const style = await page.addStyleTag({
      content: `header,footer,#loading,#debug-open,#debug-hint,#browser-controls{visibility:hidden}#scene{width:${width / dpr}px;height:${height / dpr}px;object-fit:fill}`,
    });
    await page.locator('#scene').screenshot({
      path: path.join(captureDir, `web-${backend}-${name}-pixels.png`),
    });
    await style.evaluate((node) => node.remove());
  }
  await capturePixels('day');
  if (backend === 'auto') {
    assert.equal(
      await page.locator('#backend-status').textContent(),
      'WebGL 2',
    );
    console.log(
      'Automatic WebGL 2 fallback rendered successfully without WebGPU',
    );
    return;
  }
  if (backend === 'webgpu' && !uiOnly) {
    const first = PNG.sync.read(fs.readFileSync(
      path.join(captureDir, `web-${backend}-day-pixels.png`),
    ));
    const submitted = await page.evaluate(() => window.previewControls.submittedFrames);
    await page.evaluate(() => rendererWorker.postMessage({ type: 'seek', value: 1200 }));
    await page.waitForFunction((n) => window.previewControls.submittedFrames > n,
      submitted, { timeout: 120000 });
    assert.equal(await page.evaluate(() => window.previewMetrics.frames), submitted,
      'The software GPU should still be rendering the first queued scene');
    await page.evaluate(() => rendererWorker.postMessage({ type: 'seek', value: 510 }));
    await page.waitForFunction((n) => window.previewControls.submittedFrames >= n + 2,
      submitted, { timeout: 120000 });
    assert.equal(await page.evaluate(() => window.previewControls.inFlight), 2,
      'The next scene must encode without waiting for the prior GPU frame');
    await page.waitForFunction((n) => window.previewMetrics.frames >= n + 2 &&
      Math.abs(window.previewMetrics.time - 510) < .01,
      submitted, { timeout: 240000 });
    await capturePixels('queued-return');
    const returned = PNG.sync.read(fs.readFileSync(
      path.join(captureDir, `web-${backend}-queued-return-pixels.png`),
    ));
    assert(matches(difference(first, returned)),
      'Overlapping day/night/day frames must preserve per-frame uniforms and image fidelity');
  }
  async function nativeClick(group, index) {
    const rect = await page.evaluate(
      ({ group, index }) => {
        const value = window.previewControls.layout[group];
        return index === undefined ? value : value[index];
      },
      { group, index },
    );
    await page.mouse.click(rect.x + rect.width / 2, rect.y + rect.height / 2);
  }
  async function openMenu() {
    const size = page.viewportSize();
    await page.mouse.move(size.width - 2, 2);
    await page.waitForFunction(() => window.previewControls?.prompt);
    await nativeClick('prompt');
    await page.waitForFunction(() => window.previewControls?.open);
    // Input state arrives before GPU completion; screenshots need the frame
    // that actually contains the panel, rather than its preceding invitation.
    await page.waitForFunction(() => window.previewMetrics.menuOpen, null, {
      timeout: 120000,
    });
  }
  async function closeMenu() {
    const before = await page.evaluate(() => window.previewMetrics.frames);
    await nativeClick('close');
    await page.waitForFunction(
      (n) =>
        !window.previewControls.open &&
        !window.previewMetrics.menuOpen &&
        window.previewMetrics.frames > n,
      before,
      { timeout: 120000 },
    );
  }
  const before = await page.evaluate(() => ({
    ...window.previewMetrics,
    samples: undefined,
  }));
  await page.waitForTimeout(250);
  assert.equal(
    await page.evaluate(() => window.previewMetrics.frames),
    before.frames,
    'Pause must stop GPU redraws',
  );
  assert.equal(
    await page.evaluate(() => window.previewMetrics.time),
    before.time,
    'Paused timeline must stay fixed',
  );
  const menuFrame = await page.evaluate(() => window.previewMetrics.frames);
  await openMenu();
  await page.waitForFunction(
    (n) => window.previewMetrics.frames > n,
    menuFrame,
    { timeout: 120000 },
  );
  await capturePixels('debug-menu');
  assert(await page.locator('#debug-hint').isHidden());
  await nativeClick('buttons', 0);
  await page.waitForFunction(
    (time) => window.previewMetrics.time > time,
    before.time,
  );
  await nativeClick('buttons', 0);
  await page.waitForFunction(() => window.previewMetrics.paused, null, {
    timeout: 120000,
  });
  await nativeClick('buttons', 1);
  await page.waitForFunction(() => window.previewControls.span === 1800);
  const slider = await page.evaluate(
    () => window.previewControls.layout.slider,
  );
  await page.mouse.move(
    slider.x + slider.width * 0.25,
    slider.y + slider.height / 2,
  );
  await page.mouse.down();
  await page.mouse.move(
    slider.x + slider.width * 0.5,
    slider.y + slider.height / 2,
  );
  await page.mouse.up();
  await page.waitForFunction(
    () => Math.abs(window.previewControls.fraction - 0.5) < 0.01,
  );
  await nativeClick('weather', 5);
  await page.waitForFunction(() => window.previewControls.weather === 5);
  await nativeClick('weather', 0);
  await page.waitForFunction(() => window.previewControls.weather === 0);
  await nativeClick('meteorMode', 1);
  await page.waitForFunction(() => window.previewControls.meteorMode === 1);
  await nativeClick('meteorMode', 0);
  await page.waitForFunction(() => window.previewControls.meteorMode === 0);
  await page.evaluate(() => {
    const select = document.querySelector('#resolution');
    select.add(new Option('CI resize', '270'));
  });
  await page.locator('#resolution').selectOption('270');
  await page.waitForFunction(
    () =>
      window.previewMetrics.sceneWidth === 480 &&
      window.previewMetrics.sceneHeight === 270,
  );
  await page.evaluate(() =>
    rendererWorker.postMessage({ type: 'seek', value: 1200 }),
  );
  await page.waitForFunction(
    () => Math.abs(window.previewMetrics.time - 1200) < 0.01,
  );
  await closeMenu();
  const dusk = imageStats(
    await page.screenshot({
      path: path.join(captureDir, `web-${backend}-dusk.png`),
    }),
  );
  await capturePixels('dusk');
  assert(
    Math.abs(day - dusk) / day > 0.02,
    'Seeking must visibly change the shared scene',
  );
  // Actual shared graphics page: draft edits must not allocate resources.
  await openMenu();
  await nativeClick('graphicsButton');
  await page.waitForFunction(() => window.previewMetrics.graphicsOpen, null, {
    timeout: 120000,
  });
  const revision = await page.evaluate(
    () => window.previewControls.graphics.revision,
  );
  const qualityBar = await page.evaluate(
    () => window.previewControls.layout.qualitySlider,
  );
  await page.mouse.click(qualityBar.x, qualityBar.y + qualityBar.height / 2);
  await page.waitForFunction(
    () => window.previewControls.graphics.preset === 0,
  );
  assert.equal(
    await page.evaluate(() => window.previewControls.graphics.revision),
    revision,
    'Preset draft must not rebuild',
  );
  await nativeClick('graphicsApply');
  await page.waitForFunction(
    (n) => window.previewMetrics.qualityRevision > n,
    revision,
    { timeout: 180000 },
  );
  assert.equal(
    await page.evaluate(() => window.previewMetrics.assetBytes),
    0,
    'Consumed file sources must be released',
  );
  assert.equal(
    await page.evaluate(() => window.previewControls.graphics.features[0]),
    false,
    'Low uses the bounded static landscape',
  );
  assert.equal(
    await page.evaluate(() => window.previewControls.graphics.features[1]),
    false,
    'Low skips volumetric clouds',
  );
  assert.equal(
    await page.evaluate(() => window.previewControls.graphics.features[2]),
    false,
    'Low skips galaxy allocations',
  );
  assert(
    await page.evaluate(
      () =>
        window.previewMetrics.sceneWidth * window.previewMetrics.sceneHeight <=
        640 * 360,
    ),
  );
  assert.equal(
    await page.evaluate(() => window.previewMetrics.height),
    720,
    'Low keeps the shared bitmap controls at a readable presentation resolution',
  );
  const resourceBaseline = await page.evaluate(
    () => window.previewMetrics.resources?.bytes,
  );
  for (let cycle = 0; cycle < 4; ++cycle) {
    const previous = await page.evaluate(
      () => window.previewControls.graphics.revision,
    );
    await nativeClick('features', 3); // Stars: off/on/off/on, returning to same resources.
    await nativeClick('graphicsApply');
    await page.waitForFunction(
      (n) => window.previewMetrics.qualityRevision > n,
      previous,
      { timeout: 180000 },
    );
    assert.equal(
      await page.evaluate(() => window.previewMetrics.assetBytes),
      0,
    );
  }
  if (resourceBaseline !== undefined)
    assert(
      (await page.evaluate(() => window.previewMetrics.resources.bytes)) <=
        resourceBaseline + 1024 * 1024,
      'Repeated Apply must release old GPU resources',
    );
  await page.screenshot({
    path: path.join(captureDir, `web-${backend}-graphics.png`),
  });
  await nativeClick('graphicsBack');
  await closeMenu();
  // A small diagnostic budget keeps aspect tests cheap even on software GPUs.
  await page.evaluate(() =>
    rendererWorker.postMessage({ type: 'quality', height: 270 }),
  );
  await page.waitForFunction(() => window.previewMetrics.height === 270, null, {
    timeout: 180000,
  });
  const resizeBaseline = await page.evaluate(() => ({
    bytes: window.previewMetrics.resources.bytes,
    assets: window.previewMetrics.assetDownloaded,
    builds: window.previewMetrics.builds,
  }));
  for (const [width, height] of [
    [390, 844],
    [2400, 600],
    [1280, 720],
  ]) {
    await page.setViewportSize({ width, height });
    await page.waitForFunction(
      ({ width, height }) =>
        Math.abs(
          window.previewMetrics.width / window.previewMetrics.height -
            width / height,
        ) <
        2 / window.previewMetrics.height,
      { width, height },
    );
    assert(
      await page.evaluate(
        () =>
          window.previewMetrics.width * window.previewMetrics.height <=
          480 * 270,
      ),
    );
    const bounds = await page.locator('#scene').boundingBox();
    assert.equal(bounds.width, width);
    assert.equal(bounds.height, height);
  }
  assert.deepEqual(await page.evaluate(() => ({
    bytes: window.previewMetrics.resources.bytes,
    assets: window.previewMetrics.assetDownloaded,
    builds: window.previewMetrics.builds,
  })), resizeBaseline, 'Aspect round trip must reuse assets and return to the same GPU budget');
  if (uiOnly) {
    await page.locator('#debug-open').focus();
    await page.keyboard.press('Enter');
    await page.waitForFunction(() => window.previewControls.open);
    await page.keyboard.press('Escape');
    await page.waitForFunction(() => !window.previewControls.open);
    await page.goto(
      address(
        url,
        'time=510&travel=162000&speed=1&quality=-1&memoryGiB=32&cores=16',
      ),
    );
    await page.getByRole('button', { name: 'Launch preview' }).click();
    await smallCanvas();
    await ready(page);
    await checkInitialFlightTime();
    assert.equal(
      await page.evaluate(() => window.previewControls.graphics.preset),
      0,
      'Software adapter must start Low despite generous memory/core hints',
    );
    assert.match(await page.locator('#graphics-hint').textContent(),
      /Low quality · static landscape/,
      'Auto must disclose the applied quality and missing terrain');
    assert(
      await page.evaluate(
        () => window.previewMetrics.assetDownloaded < 8 * 1024 * 1024,
      ),
      'Auto Low must not download world or galaxy',
    );
    assert.deepEqual(errors, []);
    console.log(
      backend +
        ' stripped-scene UI: native menu, scrub, weather/meteor controls, aspect, budget and keyboard access passed',
    );
    return;
  }
  for (const fixture of fixtures.filter((fixture) => fixture.name !== 'day')) {
    await page.goto(
      address(
        url,
        `paused=1&time=${fixture.time}&travel=${fixture.travel}&weather=${fixture.weather}${fixture.options || ''}`,
      ),
    );
    await page.getByRole('button', { name: 'Launch preview' }).click();
    await smallCanvas(fixture.height);
    await ready(page);
    assert.equal(
      await page.evaluate(() => window.previewMetrics.sceneHeight),
      fixture.height || 180,
      'Fidelity fixtures must render at their requested scene resolution',
    );
    await capturePixels(fixture.name);
  }
  // Force a terrain rebase and a worker build during moving flight.
  await page.goto(address(url, 'time=510&travel=162000&speed=1'));
  await page.getByRole('button', { name: 'Launch preview' }).click();
  await smallCanvas();
  await ready(page);
  await checkInitialFlightTime();
  await page.waitForFunction(
    () => window.previewMetrics.samples.some((frame) => frame.pending),
    null,
    { timeout: 240000 },
  );
  const builds = await page.evaluate(() => window.previewMetrics.builds);
  await page.waitForFunction(
    (n) => window.previewMetrics.frames > n,
    (await page.evaluate(() => window.previewMetrics.frames)) + 3,
    { timeout: 120000 },
  );
  assert(
    await page.evaluate(() => window.previewMetrics.maxInFlight <= 2),
    'GPU work must remain bounded to two submissions',
  );
  if (backend === 'webgpu')
    assert.equal(
      await page.evaluate(() => window.previewMetrics.maxInFlight), 2,
      'CPU must encode another frame while the software GPU is busy',
    );
  assert(builds >= 2, 'Moving terrain must continue streaming in WASM');
  // A second visit uses the established service worker without another reload.
  await openMenu();
  await page.getByRole('button', { name: 'Stop preview' }).click();
  await page.getByRole('button', { name: 'Launch preview' }).click();
  await smallCanvas();
  await ready(page);
  assert(await page.evaluate(() => crossOriginIsolated));
  assert.deepEqual(errors, []);
  fs.writeFileSync(
    path.join(captureDir, `web-${backend}-validation.json`),
    JSON.stringify(
      {
        url,
        day,
        dusk,
        builds,
        metrics: await page.evaluate(() => window.previewMetrics),
      },
      null,
      2,
    ),
  );
  console.log(
    backend +
      ' static WASM preview: isolation, rendering, pause/resume, seek, HiDPI pixel budget, resize, terrain worker and stop/relaunch passed',
  );
})()
  .catch(async (error) => {
    console.error(error);
    console.error('Browser errors:', errors);
    if (page)
      try {
        console.error(
          await page.evaluate(() => ({
            status: document.querySelector('#status').textContent,
            controls: window.previewControls,
            metrics: { ...window.previewMetrics, samples: undefined },
          })),
        );
        await page.screenshot({
          path: path.join(captureDir, `web-${backend}-failure.png`),
        });
      } catch {}
    process.exitCode = 1;
  })
  .finally(async () => {
    if (browser) await browser.close();
    if (server) await new Promise((resolve) => server.close(resolve));
  });
