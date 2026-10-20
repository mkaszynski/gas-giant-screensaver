// CI only: render the desktop CLI sequentially, then compare actual pixels.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { PNG } = require('../web/node_modules/pngjs');
const fixtures = require('./scene_fixtures.cjs');

function difference(a, b, startRow = 0) {
  assert.equal(a.width, b.width, 'Same physical render width');
  assert.equal(a.height, b.height, 'Same physical render height');
  let total = 0, peak = 0, outliers = 0, count = 0;
  for (let i = startRow * a.width * 4; i < a.data.length; ++i) {
    if (i % 4 === 3) continue;
    const delta = Math.abs(a.data[i] - b.data[i]);
    total += delta;
    peak = Math.max(peak, delta);
    outliers += delta > 8;
    ++count;
  }
  return { mean: total / count, peak, above8: outliers / count };
}
function matches(metrics, mean = 1, outliers = .001) {
  return metrics.mean <= mean && metrics.above8 <= outliers;
}
function detailRatio(a, b, startRow) {
  function energy(image) {
    let sum = 0;
    for (let y = startRow; y < image.height - 1; ++y)
      for (let x = 0; x < image.width - 1; ++x)
        for (let c = 0; c < 3; ++c) {
          const i = (y * image.width + x) * 4 + c;
          sum += Math.abs(image.data[i] - image.data[i + 4]) +
            Math.abs(image.data[i] - image.data[i + image.width * 4]);
        }
    return sum;
  }
  const native = energy(a), browser = energy(b);
  return native ? browser / native : (browser ? Infinity : 1);
}
function matchesLandscape(metrics) {
  // Mesa/SwiftShader baseline: mean <=1.43, >8-code channels <=0.315%,
  // detail energy 0.9796..1.0102. Keep the full-image gate tighter and test
  // sharpness independently, so accommodating driver raster/filter precision
  // cannot conceal a lower-resolution or blurred landscape.
  return matches(metrics, 2, .005) &&
    metrics.detailRatio >= .95 && metrics.detailRatio <= 1.05;
}

function main() {
  const directory = path.resolve(__dirname, '../captures');
  const binary = path.resolve(process.env.NATIVE_RENDERER || 'build-native/gas-giant-screensaver');
  const report = {};
  const failures = [];
  for (const fixture of fixtures) {
    const height = fixture.height || 180, width = height * 16 / 9;
    const file = path.join(directory, `native-${fixture.name}-pixels.png`);
    const options = new URLSearchParams(fixture.options || '');
    const args = [
      '--capture', file, '--time', String(fixture.time),
      '--travel-time', String(fixture.travel), '--weather-time', String(fixture.weather),
      '--width', String(width), '--height', String(height),
    ];
    if (options.get('surface') === '0') args.push('--fixed-view');
    if (options.get('stars') === '0') args.push('--no-stars');
    if (options.get('volumetric') === '0') args.push('--no-weather');
    // No --quality flag: the reference is the desktop's original full renderer.
    const run = spawnSync('xvfb-run', ['-a', binary, ...args], {
      env: { ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2' },
      encoding: 'utf8', timeout: 180000,
    });
    assert.equal(run.status, 0, run.error?.message || run.stderr);
    const native = PNG.sync.read(fs.readFileSync(file));
    assert.equal(native.width, width);
    assert.equal(native.height, height);
    for (const backend of ['webgl2', 'webgpu']) {
      const browser = PNG.sync.read(fs.readFileSync(
        path.join(directory, `web-${backend}-${fixture.name}-pixels.png`),
      ));
      const landscapeRow = Math.floor(height * .8);
      const metrics = {
        image: difference(native, browser),
        // A large, matching sky must not hide a missing or degraded landscape.
        landscape: {
          ...difference(native, browser, landscapeRow),
          detailRatio: detailRatio(native, browser, landscapeRow),
        },
        width, height,
      };
      report[`${backend}-${fixture.name}`] = metrics;
      if (!matches(metrics.image) || !matchesLandscape(metrics.landscape))
        failures.push(`${backend}-${fixture.name}`);
    }
  }
  fs.writeFileSync(path.join(directory, 'web-native-parity.json'),
    JSON.stringify(report, null, 2) + '\n');
  console.log('Native / WASM fidelity:', report);
  assert.deepEqual(failures, [], 'WASM must preserve desktop image fidelity at equal settings');
}
module.exports = { difference, detailRatio, matches, matchesLandscape };
if (require.main === module) main();
