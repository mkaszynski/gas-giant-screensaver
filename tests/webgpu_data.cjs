const assert = require('node:assert/strict');
const { float16, writeUniform, sourceKey } = require('../web/graphics-data.js');
for (const [value, expected] of [
  [0, 0],
  [-0, 0x8000],
  [1, 0x3c00],
  [-2, 0xc000],
  [65504, 0x7bff],
  [Infinity, 0x7c00],
  [-Infinity, 0xfc00],
  [2 ** -14, 0x400],
  [2 ** -24, 1],
  [2 ** -25, 0],
  [3 * 2 ** -25, 2],
  [1 + 2 ** -11, 0x3c00],
  [1 + 3 * 2 ** -11, 0x3c02],
  [3.9999, 0x4400],
])
  assert.equal(float16(value), expected, String(value));
assert.equal(float16(NaN) & 0x7e00, 0x7e00);
assert.notEqual(
  sourceKey('void main() { /* comment */ color = 1.; }'),
  sourceKey('void main(){color=1.;}//comment'),
);
assert.notEqual(sourceKey('color=1.;'), sourceKey('color=2.;'));
const buffer = new ArrayBuffer(256),
  f = new Float32Array(buffer),
  i = new Int32Array(buffer);
writeUniform(
  buffer,
  { offset: 16, type: 'vec2', array_stride: 16 },
  [1, 2, 3, 4],
  0,
  4,
);
assert.deepEqual([...f.slice(4, 12)], [1, 2, 0, 0, 3, 4, 0, 0]);
writeUniform(
  buffer,
  { offset: 48, type: 'mat3', matrix_stride: 16 },
  [1, 2, 3, 4, 5, 6, 7, 8, 9],
  0,
  9,
  3,
);
assert.deepEqual([...f.slice(12, 24)], [1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0]);
writeUniform(buffer, { offset: 112, type: 'ivec4' }, [-1, 2, 3, 4], 0, 4);
assert.deepEqual([...i.slice(28, 32)], [-1, 2, 3, 4]);
writeUniform(buffer, { offset: 128, type: 'uint' }, [1], 0, 1);
assert.equal(new DataView(buffer).getUint32(128, true), 1);
// Partial Emscripten heap views and nonzero array elements preserve neighbors.
writeUniform(
  buffer,
  { offset: 144, type: 'vec4', array_stride: 16 },
  [999, 8, 7, 6, 5, 999],
  1,
  4,
  0,
  1,
);
assert.deepEqual([...f.slice(36, 44)], [0, 0, 0, 0, 8, 7, 6, 5]);
assert.throws(
  () => writeUniform(buffer, { offset: 256, type: 'float' }, [1], 0, 1),
  RangeError,
);
console.log(
  'WebGPU shader identities, IEEE half conversion and std140 uniform uploads passed',
);

assert.equal(sourceKey('a\r\nb\r'), sourceKey('a\nb\n'));
assert.notEqual(sourceKey('#define A B\nC'), sourceKey('#define A BC'));
