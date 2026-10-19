// Sequential, headless feature isolation. Run inside scripts/browser_budget.py.
const assert = require('node:assert/strict');
const fs = require('node:fs'),
  path = require('node:path'),
  http = require('node:http');
const { chromium } = require('../web/node_modules/playwright');
const root = path.resolve(process.env.PROFILE_BUILD || 'build-web');
const backend = process.env.PREVIEW_BACKEND || 'webgl2';
const all = [
  [
    'minimal',
    {
      surface: 0,
      volumetric: 0,
      galaxy: 0,
      stars: 0,
      rings: 0,
      emissions: 0,
      meteors: 0,
    },
  ],
  [
    'stars',
    {
      surface: 0,
      volumetric: 0,
      galaxy: 0,
      stars: 1,
      rings: 0,
      emissions: 0,
      meteors: 0,
    },
  ],
  [
    'galaxy',
    {
      surface: 0,
      volumetric: 0,
      galaxy: 1,
      stars: 0,
      rings: 0,
      emissions: 0,
      meteors: 0,
    },
  ],
  [
    'terrain',
    {
      surface: 1,
      volumetric: 0,
      galaxy: 0,
      stars: 0,
      rings: 0,
      emissions: 0,
      meteors: 0,
    },
  ],
  [
    'clouds',
    {
      surface: 1,
      volumetric: 1,
      galaxy: 0,
      stars: 0,
      rings: 0,
      emissions: 0,
      meteors: 0,
    },
  ],
  ['complete', {}],
  ...(process.env.PROFILE_VERSION === 'before'
    ? []
    : [['auto-low', { quality: -1, height: 360, memoryGiB: 0, cores: 0 }]]),
];
const selected = process.env.PROFILE_STAGES?.split(',');
const report = {
  backend,
  stages: [],
  note: 'GPU bytes are requested resource sizes, excluding driver overhead; cgroup memory includes Node and browser.',
};
function cgroup() {
  try {
    const entry = fs
      .readFileSync('/proc/self/cgroup', 'utf8')
      .split('\n')
      .find((s) => s.startsWith('0::'));
    const folder = path.join('/sys/fs/cgroup', entry.slice(3));
    return {
      current: Number(
        fs.readFileSync(path.join(folder, 'memory.current'), 'utf8'),
      ),
      peak: Number(fs.readFileSync(path.join(folder, 'memory.peak'), 'utf8')),
    };
  } catch {
    return null;
  }
}
let server, browser, stageTimer;
function percentile(a, p) {
  a = a.slice().sort((x, y) => x - y);
  return a[Math.min(a.length - 1, Math.floor(a.length * p))];
}
(async () => {
  server = http.createServer((req, res) => {
    const name =
      decodeURIComponent(
        new URL(req.url, 'http://localhost').pathname.slice(
          '/gas-giant-screensaver/'.length,
        ),
      ) || 'index.html';
    const file = path.resolve(root, name);
    if (
      !file.startsWith(root + path.sep) ||
      !fs.existsSync(file) ||
      !fs.statSync(file).isFile()
    ) {
      res.writeHead(404).end();
      return;
    }
    res.writeHead(200, {
      'Content-Type':
        {
          '.html': 'text/html',
          '.js': 'text/javascript',
          '.css': 'text/css',
          '.wasm': 'application/wasm',
          '.json': 'application/json',
        }[path.extname(file)] || 'application/octet-stream',
    });
    fs.createReadStream(file).pipe(res);
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  for (const [name, options] of all) {
    if (selected && !selected.includes(name)) continue;
    stageTimer = setTimeout(() => {
      console.error(
        'Feature stage exceeded its 180-second safety deadline: ' + name,
      );
      process.exit(124);
    }, 180000);
    stageTimer.unref();
    const start = performance.now(),
      samples = [];
    const sampler = setInterval(() => {
      const m = cgroup();
      if (m) samples.push(m);
    }, 100);
    sampler.unref();
    browser = await chromium.launch({
      executablePath: process.env.CHROMIUM_PATH || undefined,
      headless: true,
      args: [
        '--no-sandbox',
        '--enable-unsafe-swiftshader',
        '--use-angle=swiftshader',
        '--ignore-gpu-blocklist',
        ...[
          '--enable-unsafe-webgpu',
          '--enable-features=Vulkan',
          '--use-vulkan=swiftshader',
        ],
      ],
    });
    const page = await browser.newPage({
      viewport: { width: 1280, height: 720 },
    });
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));
    const query = new URLSearchParams({
      backend,
      time: 510,
      travel: 162000,
      weather: 1790900000,
      quality: 2,
      height: 180,
      speed: 0,
      ...options,
    });
    await page.goto(
      `http://127.0.0.1:${server.address().port}/gas-giant-screensaver/?${query}`,
    );
    await page
      .getByRole('button', { name: 'Launch preview' })
      .click({ timeout: 60000 });
    // Old builds use a browser resolution selector, retained for baseline runs.
    await page.evaluate(() => {
      const select = document.querySelector('#resolution');
      const height =
        new URLSearchParams(location.search).get('height') || '180';
      select.add(new Option('Profile', height));
      select.value = height;
      select.dispatchEvent(new Event('change'));
    });
    await page.waitForFunction(
      () =>
        window.previewMetrics?.frames >= 1 ||
        document.querySelector('h2')?.textContent === 'Preview unavailable',
      null,
      { timeout: 180000 },
    );
    assert.notEqual(
      await page.locator('h2').textContent(),
      'Preview unavailable',
      await page.locator('#status').textContent(),
    );
    const firstFrameMs = performance.now() - start;
    for (let i = 0; i < 12; i++) {
      await page.waitForTimeout(250);
    }
    await page.waitForFunction(() => window.previewMetrics?.frames >= 8, null, {
      timeout: 180000,
    });
    assert.deepEqual(errors, []);
    const metrics = await page.evaluate(() => window.previewMetrics);
    const session = await browser.newBrowserCDPSession();
    const processInfo = await session.send('SystemInfo.getProcessInfo');
    const processes = processInfo.processInfo.map((info) => {
      const result = { type: info.type, cpuSeconds: info.cpuTime };
      try {
        const status = fs.readFileSync(`/proc/${info.id}/status`, 'utf8');
        result.rss = Number(/^VmRSS:\s+(\d+)/m.exec(status)?.[1]) * 1024;
      } catch {}
      try {
        const status = fs.readFileSync(`/proc/${info.id}/smaps_rollup`, 'utf8');
        result.pss = Number(/^Pss:\s+(\d+)/m.exec(status)?.[1]) * 1024;
      } catch {}
      return result;
    });
    await session.detach();
    const timings = metrics.samples.slice(2);
    const stage = {
      name,
      firstFrameMs,
      processes,
      cgroup: samples.filter(Boolean),
      sampledPeak: Math.max(...samples.map((m) => m.current)),
      frames: metrics.frames,
      cpuMedianMs: percentile(
        timings.map((f) => f.cost),
        0.5,
      ),
      completionP95Ms: percentile(
        timings.map((f) => f.latency || f.interval),
        0.95,
      ),
      allocated: metrics.allocated,
      heap: metrics.heap,
      resources: metrics.resources,
      assetBytes: metrics.assetBytes,
      assetDownloaded: metrics.assetDownloaded,
    };
    report.stages.push(stage);
    fs.mkdirSync('captures', { recursive: true });
    fs.writeFileSync(
      process.env.PROFILE_REPORT || `captures/web-memory-${backend}.json`,
      JSON.stringify(report, null, 2) + '\n',
    );
    console.log(JSON.stringify({ ...stage, cgroup: stage.cgroup.at(-1) }));
    await browser.close();
    browser = null;
    clearInterval(sampler);
    clearTimeout(stageTimer);
  }
})()
  .catch((error) => {
    console.error(error);
    process.exitCode = 1;
  })
  .finally(async () => {
    if (browser) await browser.close();
    if (server) await new Promise((resolve) => server.close(resolve));
    fs.mkdirSync('captures', { recursive: true });
    fs.writeFileSync(
      process.env.PROFILE_REPORT || `captures/web-memory-${backend}.json`,
      JSON.stringify(report, null, 2) + '\n',
    );
  });
