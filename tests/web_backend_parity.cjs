const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { PNG } = require('../web/node_modules/pngjs');
const directory = path.resolve(__dirname, '../captures');
const report = {};
for (const fixture of ['day', 'dusk', 'night', 'night-no-stars', 'storm', 'thin-clouds']) {
  const images = ['webgl2', 'webgpu'].map((backend) =>
    PNG.sync.read(
      fs.readFileSync(
        path.join(directory, `web-${backend}-${fixture}-pixels.png`),
      ),
    ),
  );
  const [a, b] = images;
  assert.equal(a.width, b.width);
  assert.equal(a.height, b.height);
  let total = 0,
    peak = 0,
    outliers = 0,
    count = 0;
  for (let i = 0; i < a.data.length; i++) {
    if (i % 4 === 3) continue;
    const difference = Math.abs(a.data[i] - b.data[i]);
    total += difference;
    peak = Math.max(peak, difference);
    outliers += difference > 8;
    count++;
  }
  report[fixture] = {
    width: a.width,
    height: a.height,
    mean: total / count,
    peak,
    above8: outliers / count,
  };
  // Allow low-level floating-point/dither differences, retaining every raw peak.
  assert(
    total / count <= 1,
    `${fixture}: average backend error exceeds one 8-bit code`,
  );
  assert(
    outliers / count <= 0.001,
    `${fixture}: more than 0.1% of channels differ by over eight codes`,
  );
}
for (const backend of ['webgl2', 'webgpu']) {
  const on = PNG.sync.read(
    fs.readFileSync(path.join(directory, `web-${backend}-night-pixels.png`)),
  );
  const off = PNG.sync.read(
    fs.readFileSync(
      path.join(directory, `web-${backend}-night-no-stars-pixels.png`),
    ),
  );
  let sum = 0,
    peak = 0;
  for (let i = 0; i < on.data.length; i++)
    if (i % 4 !== 3) {
      const delta = Math.max(0, on.data[i] - off.data[i]);
      sum += delta;
      peak = Math.max(peak, delta);
    }
  report[backend + '-star-signal'] = { sum, peak };
  assert(
    sum >= 10 && peak >= 2,
    backend +
      ': catalog star sprites must visibly contribute in the eclipse fixture',
  );
}
fs.writeFileSync(
  path.join(directory, 'web-backend-parity.json'),
  JSON.stringify(report, null, 2),
);
console.log('WebGPU / WebGL 2 scene parity passed', report);
