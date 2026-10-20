// Exercise worker context selection without starting a browser or GPU.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../web/render-worker.js'), 'utf8');
async function start(backend) {
  const messages = [], requests = [];
  const gl = {
    RENDERER: 0x1f01,
    getExtension: () => null,
    getParameter: () => 'SwiftShader Device',
  };
  const worker = {
    addEventListener() {},
    postMessage: (message) => messages.push(message),
    location: { href: 'https://example.test/gas-giant-screensaver/render-worker.js' },
  };
  const context = vm.createContext({
    self: worker, console, URL,
    importScripts() {},
    ObservatoryGPUMemory: { instrumentGL: (value) => assert.equal(value, gl) },
    createObservatoryGPU: async () => {
      const error = new Error('Unavailable');
      error.webgpuUnavailable = true;
      throw error;
    },
  });
  vm.runInContext(source, context);
  await worker.onmessage({ data: {
    type: 'start', options: { backend },
    canvas: {
      addEventListener() {},
      getContext(type, options) {
        requests.push({ type, options });
        return gl;
      },
    },
  } });
  assert.equal(requests.length, 1);
  assert.equal(requests[0].type, 'webgl2');
  assert.equal(requests[0].options.powerPreference, 'high-performance');
  assert.equal(worker.Module.preinitializedWebGLContext, gl);
  assert.equal(worker.Module.previewOptions.software, '1');
  assert(messages.some((message) => message.type === 'backend' && message.backend === 'webgl2'));
  if (backend === 'auto')
    assert(messages.some((message) => message.type === 'status'));
}
(async () => {
  await start('webgl2');
  await start('auto');
  console.log('Worker: accelerated WebGL request, capability hints and automatic fallback passed');
})().catch((error) => { console.error(error); process.exitCode = 1; });
