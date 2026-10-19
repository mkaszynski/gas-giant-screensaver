const assert = require('node:assert/strict');
const { textureBytes, instrument } = require('../web/gpu-memory.js');
assert.equal(
  textureBytes({
    size: [8, 8, 8],
    format: 'rgba8unorm',
    dimension: '3d',
    mipLevelCount: 4,
  }),
  2340,
);
assert.equal(
  textureBytes({
    size: [8, 8, 6],
    format: 'bc6h-rgb-ufloat',
    mipLevelCount: 4,
  }),
  672,
);
assert.equal(
  textureBytes({ size: [100, 50], format: 'rgba16float', sampleCount: 4 }),
  160000,
);
const device = {
  createBuffer: () => ({ destroy() {} }),
  createTexture: () => ({ destroy() {} }),
};
const stats = instrument(device);
const b = device.createBuffer({ size: 128 });
const t = device.createTexture({ size: [4, 4], format: 'rgba8unorm' });
assert.deepEqual(stats(), { bytes: 192, peak: 192, buffers: 1, textures: 1 });
b.destroy();
b.destroy();
t.destroy();
assert.deepEqual(stats(), { bytes: 0, peak: 192, buffers: 0, textures: 0 });
assert.throws(
  () => device.createBuffer({ size: 1024 * 1024 * 1024 }),
  /budget exceeded/,
);
assert.equal(stats().bytes, 0, 'Budget rejection precedes allocation');
const { instrumentGL } = require('../web/gpu-memory.js');
let next = 1;
const gl = {
  TEXTURE0: 100,
  TEXTURE_2D: 1,
  TEXTURE_3D: 2,
  TEXTURE_CUBE_MAP: 3,
  TEXTURE_CUBE_MAP_POSITIVE_X: 10,
  TEXTURE_CUBE_MAP_NEGATIVE_Z: 15,
  RGBA8: 20,
  ARRAY_BUFFER: 30,
  ELEMENT_ARRAY_BUFFER: 31,
};
for (const name of ['createBuffer', 'createTexture', 'createRenderbuffer'])
  gl[name] = () => ({ id: next++ });
for (const name of [
  'deleteBuffer',
  'deleteTexture',
  'deleteRenderbuffer',
  'deleteVertexArray',
  'bindVertexArray',
  'bindBuffer',
  'bufferData',
  'activeTexture',
  'bindTexture',
  'texImage2D',
  'compressedTexImage2D',
  'texImage3D',
  'texStorage3D',
  'generateMipmap',
  'bindRenderbuffer',
  'renderbufferStorage',
  'renderbufferStorageMultisample',
])
  gl[name] = () => {};
instrumentGL(gl);
const texture = gl.createTexture();
gl.bindTexture(gl.TEXTURE_2D, texture);
gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 8, 8, 0, gl.RGBA8, 0, null);
gl.generateMipmap(gl.TEXTURE_2D);
assert.equal(gl.memoryStats().bytes, 340);
const buffer = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(200), 0, 10, 20);
assert.equal(
  gl.memoryStats().bytes,
  360,
  'Upload subrange, not entire WASM heap, is counted',
);
assert.throws(
  () => gl.bufferData(gl.ARRAY_BUFFER, 1024 * 1024 * 1024, 0),
  /budget exceeded/,
);
gl.deleteBuffer(buffer);
gl.deleteTexture(texture);
assert.equal(gl.memoryStats().bytes, 0);
console.log(
  'GPU ledger: mip volumes, compressed arrays, MSAA and idempotent resource retirement passed',
);
