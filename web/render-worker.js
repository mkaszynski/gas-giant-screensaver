'use strict';
// Only browser hosting lives here. Simulation, meshes and rendering stay in C++.
self.addEventListener('error', event => console.error(event.error?.stack || event.message));
let canvas;
let ready = false;
const pending = [];
let failed = false;
self.previewError = message => {
  if (failed) return;
  failed = true;
  console.error(message);
  self.postMessage({type: 'error', message});
};
self.previewFrame = frame => self.postMessage({type: 'frame', frame});
function command(data) {
  if (data.type === 'resize') {
    canvas.width = data.width;
    canvas.height = data.height;
    return;
  }
  if (!ready) { pending.push(data); return; }
  if (data.type === 'pause') Module._preview_pause(data.value ? 1 : 0);
  else if (data.type === 'seek') Module._preview_seek(data.value);
  else if (data.type === 'visibility') Module._preview_visibility(data.value ? 1 : 0);
}
self.onmessage = async ({data}) => {
  if (data.type !== 'start') { command(data); return; }
  canvas = data.canvas;
  canvas.addEventListener('webglcontextlost', event => {
    event.preventDefault();
    self.previewError('The graphics context was lost. Reload to restart the preview.');
  });
  let context;
  let backend = data.options.backend || 'auto';
  try {
    if (!['auto', 'webgpu', 'webgl2'].includes(backend)) throw new Error('Unknown graphics backend.');
    if (backend !== 'webgl2') {
      importScripts('graphics-data.js', 'webgpu-context.js');
      try {
        context = await createObservatoryGPU(canvas, self.previewError);
        backend = 'webgpu';
      } catch (error) {
        if (backend === 'webgpu' || !error.webgpuUnavailable) throw error;
        self.postMessage({type: 'status', message: 'WebGPU unavailable; using WebGL 2.'});
      }
    }
    if (!context) {
      context = canvas.getContext('webgl2', {alpha: false, antialias: false, depth: false});
      if (!context) throw new Error('WebGL 2 is unavailable.');
      backend = 'webgl2';
    }
    if (backend === 'webgpu' && data.options.benchmark === '1')
      throw new Error('Synchronous benchmark mode requires backend=webgl2. WebGPU completion is asynchronous.');
  } catch (error) { self.previewError(error.message); return; }
  self.postMessage({type: 'backend', backend});
  self.Module = {
    preinitializedWebGLContext: context,
    previewBackend: backend,
    canvas,
    previewOptions: data.options,
    mainScriptUrlOrBlob: new URL('observatory.js', self.location.href).href,
    preRun: [() => {
      // Register the transferred canvas with Emscripten's HTML5 target lookup.
      GL.offscreenCanvases.scene = canvas;
    }],
    onRuntimeInitialized() {
      ready = true;
      command({type: 'visibility', value: data.hidden});
      pending.splice(0).forEach(command);
    },
    setStatus(message) { if (message) self.postMessage({type: 'status', message}); },
    printErr(message) { console.error(message); },
    onAbort(message) { self.previewError(String(message)); }
  };
  try { importScripts('observatory.js'); }
  catch (error) { console.error(error.stack); self.previewError(error.message); }
};
