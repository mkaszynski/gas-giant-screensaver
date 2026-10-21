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
    const texture = { destroy() {}, createView() { return { texture }; } };
    return texture;
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
  // A completed older submission must never reset the next CPU frame's arena.
  const completions = [], uploads = [], submissions = [];
  device.createCommandEncoder = () => ({ finish: () => ({}) });
  device.queue.submit = (commands) => submissions.push(commands);
  device.queue.onSubmittedWorkDone = () => new Promise((resolve) => completions.push(resolve));
  device.queue.writeBuffer = (buffer, offset, data, start, length) =>
    uploads.push(Uint8Array.from(new Uint8Array(data, start, length)));
  new Uint8Array(gl.uniformBytes).fill(11, 0, 16);
  gl.uniformCursor = 16;
  gl.commands();
  const first = gl.submit();
  assert.equal(gl.uniformCursor, 0, 'Submission frees the bounded CPU arena immediately');
  new Uint8Array(gl.uniformBytes).fill(22, 0, 32);
  gl.uniformCursor = 32;
  gl.commands();
  const second = gl.submit();
  assert.equal(submissions.length, 2, 'CPU encodes the next frame before GPU completion');
  assert(uploads[0].every((byte) => byte === 11));
  assert(uploads[1].every((byte) => byte === 22));
  gl.uniformCursor = 64;
  completions.shift()();
  await first;
  assert.equal(gl.uniformCursor, 64, 'Older completion cannot clobber current-frame offsets');
  completions.shift()();
  await second;
  assert.equal(gl.uniformCursor, 64);
  assert.equal(gl.memoryStats().bytes, baseline, 'Overlap needs no extra GPU uniform buffer');
  // Repeated cloud-light slices share textures, but changed bindings, storage
  // or sampler state must immediately replace the cached texture group.
  device.createRenderPipeline = descriptor => descriptor;
  device.createBindGroup = descriptor => descriptor;
  device.createSampler = descriptor => descriptor;
  const bound = [];
  gl.beginPass = () => ({
    setPipeline() {}, setViewport() {}, setScissorRect() {},
    setBindGroup(slot, group) { if (slot === 1) bound.push(group); },
  });
  const program = gl.createProgram();
  program.definition = {
    label: 'texture-state fixture', points: false,
    vert: { module: {}, inputs: [] },
    frag: { module: {}, samplers: [
      {name: 'uImage', type: 'sampler2D', binding: 0, sampled: true},
    ] },
  };
  for (const stage of ['vert', 'frag'])
    program[stage] = {data: new ArrayBuffer(0), definition: {blockSize: 0}};
  program.pipelineLayout = {};
  program.textureLayout = {};
  program.locations.set('uImage', {program, base: 'uImage', sampler: true, unit: 0});
  gl.useProgram(program);
  const image = gl.createTexture();
  gl.bindTexture(constants.TEXTURE_2D, image);
  gl.texStorage2D(constants.TEXTURE_2D, 1, constants.RGBA8, 4, 4);
  let lookups = 0;
  const sampler = gl.sampler.bind(gl);
  gl.sampler = (...args) => { ++lookups; return sampler(...args); };
  gl.bindDraw(constants.TRIANGLES);
  const firstGroup = bound.at(-1);
  gl.bindDraw(constants.TRIANGLES);
  assert.equal(bound.at(-1), firstGroup);
  assert.equal(lookups, 1, 'Stable draws skip sampler/descriptor reconstruction');
  gl.texParameteri(constants.TEXTURE_2D, constants.TEXTURE_WRAP_S, constants.REPEAT);
  gl.bindDraw(constants.TRIANGLES);
  assert.notEqual(bound.at(-1), firstGroup, 'Sampler edits invalidate the fast path');
  const editedGroup = bound.at(-1);
  gl.texParameteri(constants.TEXTURE_2D, constants.TEXTURE_WRAP_S, constants.REPEAT);
  gl.bindDraw(constants.TRIANGLES);
  assert.equal(bound.at(-1), editedGroup);
  assert.equal(lookups, 2, 'An identical parameter assignment preserves cached state');
  gl.texStorage2D(constants.TEXTURE_2D, 1, constants.RGBA8, 8, 4);
  gl.bindDraw(constants.TRIANGLES);
  assert.notEqual(bound.at(-1), editedGroup, 'New storage cannot retain an old view');
  assert.equal(bound.at(-1).entries[0].resource.texture, image.gpu);
  const other = gl.createTexture();
  gl.activeTexture(constants.TEXTURE0 + 1);
  gl.bindTexture(constants.TEXTURE_2D, other);
  gl.texStorage2D(constants.TEXTURE_2D, 1, constants.RGBA8, 4, 4);
  gl.uniform1i(gl.getUniformLocation(program, 'uImage'), 1);
  gl.bindDraw(constants.TRIANGLES);
  assert.equal(bound.at(-1).entries[0].resource.texture, other.gpu,
    'A sampler-unit change resolves the current texture');
  gl.uniform1i(gl.getUniformLocation(program, 'uImage'), 0);
  gl.bindTexture(constants.TEXTURE_2D, image);
  gl.uniform1i(gl.getUniformLocation(program, 'uImage'), 1);
  gl.bindDraw(constants.TRIANGLES);
  assert.equal(bound.at(-1).entries[0].resource.texture, image.gpu,
    'Rebinding the same unit invalidates the previous texture identity');
  console.log(
    'WebGPU adapter: targets, light cache, software hints, retirement and overlapping uniform frames passed',
  );
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
