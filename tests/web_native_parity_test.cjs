const assert = require('node:assert/strict');
const { difference, matches } = require('./web_native_parity.cjs');
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
console.log('Native parity comparison: rounding, landscape loss and resolution mismatch passed');
