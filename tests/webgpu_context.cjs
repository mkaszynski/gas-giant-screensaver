// Exercise the real adapter against a recording device, without launching a GPU.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.join(__dirname, '../web');
const source = fs.readFileSync(path.join(root, 'webgpu-context.js'), 'utf8');
const constants = {};
for (const [, name] of source.matchAll(/\bG\.([A-Z][A-Z_0-9]*)/g))
  constants[name] ??= Object.keys(constants).length + 100;
const descriptors = [],
  writes = [],
  adapterRequests = [];
const device = {
  features: new Set(),
  createBuffer: () => ({ destroy() {} }),
  createTexture(descriptor) {
    descriptors.push(descriptor);
    return { destroy() {} };
  },
  queue: {
    onSubmittedWorkDone: () => Promise.resolve(),
    writeTexture(destination, data, layout, size) {
      writes.push({ destination, bytes: [...data], layout, size });
    },
  },
  addEventListener() {},
  lost: new Promise(() => {}),
};
const adapter = {
  info: { description: 'SwiftShader Device (Subzero)' },
  features: new Set(),
  limits: {
    maxSampledTexturesPerShaderStage: 32,
    maxSamplersPerShaderStage: 32,
  },
  requestDevice: async () => device,
};
const context = vm.createContext({
  WebGL2RenderingContext: constants,
  GPUTextureUsage: {
    TEXTURE_BINDING: 1,
    COPY_DST: 2,
    COPY_SRC: 4,
    RENDER_ATTACHMENT: 8,
    STORAGE_BINDING: 16,
  },
  GPUBufferUsage: { UNIFORM: 1, COPY_DST: 2 },
  navigator: {
    gpu: {
      requestAdapter: async (options) => {
        adapterRequests.push(options);
        return adapter;
      },
      getPreferredCanvasFormat: () => 'bgra8unorm',
    },
  },
  fetch: async () => ({ ok: true, json: async () => ({}) }),
});
for (const name of ['graphics-data.js', 'gpu-memory.js', 'webgpu-context.js'])
  vm.runInContext(fs.readFileSync(path.join(root, name), 'utf8'), context, {
    filename: name,
  });
(async () => {
  const gl = await context.createObservatoryGPU(
    {
      width: 32,
      height: 16,
      getContext: () => ({ configure() {} }),
    },
    (message) => {
      throw new Error(message);
    },
  );
  assert(
    gl.softwareAdapter,
    'Software capability reaches the worker-facing context',
  );
  assert.equal(
    adapterRequests[0].powerPreference,
    'high-performance',
    'The preview must not steer dual-GPU machines to their low-power adapter',
  );
  const baseline = gl.memoryStats().bytes;
  const color = gl.createTexture();
  gl.bindTexture(constants.TEXTURE_2D, color);
  gl.texStorage2D(constants.TEXTURE_2D, 3, constants.RGBA8, 16, 8);
  assert.equal(descriptors.at(-1).dimension, '2d');
  assert.equal(descriptors.at(-1).mipLevelCount, 3);
  assert.equal(gl.memoryStats().bytes - baseline, 672);
  gl.deleteTexture(color);
  assert.equal(
    gl.memoryStats().bytes - baseline,
    672,
    'Retired resources remain charged until queue completion',
  );
  await gl.submit();
  assert.equal(
    gl.memoryStats().bytes,
    baseline,
    'Deletion-only submission releases storage',
  );
  const light = gl.createTexture();
  gl.bindTexture(constants.TEXTURE_3D, light);
  gl.texImage3D(
    constants.TEXTURE_3D,
    0,
    constants.RG16F,
    8,
    4,
    2,
    0,
    constants.RG,
    constants.HALF_FLOAT,
    null,
  );
  assert.equal(descriptors.at(-1).dimension, '3d');
  assert.equal(
    writes.length,
    1,
    'Untouched renderable light cache is initialized before sampling',
  );
  assert.equal(writes[0].bytes.length, 256);
  assert(writes[0].bytes.every((byte) => byte === 0));
  assert.equal(writes[0].layout.bytesPerRow, 32);
  gl.deleteTexture(light);
  await gl.submit();
  assert.equal(gl.memoryStats().bytes, baseline);
  console.log(
    'WebGPU adapter: immutable targets, zeroed light cache, software hints and retirement passed',
  );
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
