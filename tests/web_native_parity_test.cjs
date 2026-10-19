const assert = require('node:assert/strict');
const { difference, detailRatio, matches, matchesLandscape } = require('./web_native_parity.cjs');
const image = () => ({ width: 16, height: 10, data: Buffer.alloc(16 * 10 * 4, 128) });
const native = image(), browser = image();
assert(matches(difference(native, browser)));
browser.data[0]++;
assert(matches(difference(native, browser)), 'Isolated one-code rounding is tolerated');
for (let i = 8 * 16 * 4; i < browser.data.length; ++i)
  browser.data[i] = 0;
assert(!matches(difference(native, browser, 8)), 'Missing landscape must fail');
browser.width = 8;
assert.throws(() => difference(native, browser), /width/);
const detailed = image(), blurred = image();
for (let y = 0; y < detailed.height; ++y)
  for (let x = 0; x < detailed.width; ++x)
    for (let c = 0; c < 3; ++c)
      detailed.data[(y * detailed.width + x) * 4 + c] = (x + y) % 2 ? 127 : 129;
const lostDetail = {
  ...difference(detailed, blurred, 8), detailRatio: detailRatio(detailed, blurred, 8),
};
assert(matches(lostDetail), 'Subtle blur can pass a color-error-only check');
assert(!matchesLandscape(lostDetail), 'Landscape detail loss must independently fail');
assert(matchesLandscape({ ...difference(detailed, detailed, 8), detailRatio: 1 }));
console.log('Native parity comparison: rounding, landscape/detail loss and resolution mismatch passed');
