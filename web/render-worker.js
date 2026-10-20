'use strict';
// Only browser hosting lives here. Simulation, meshes and rendering stay in C++.
self.addEventListener('error', (event) =>
  console.error(event.error?.stack || event.message),
);
let canvas;
let ready = false;
const pending = [];
let failed = false;
self.previewError = (message) => {
  if (failed) return;
  failed = true;
  console.error(message);
  self.postMessage({ type: 'error', message });
};
let assetPaths = [],
  assetBytes = 0,
  assetDownloaded = 0,
  assetManifest;
self.previewFrame = (frame) =>
  self.postMessage({
    type: 'frame',
    frame: { ...frame, assetBytes, assetDownloaded },
  });
self.previewLoadAssets = async (groups) => {
  try {
    self.postMessage({
      type: 'status',
      message: 'Loading selected scene resources…',
    });
    assetManifest ||= await (await fetch('assets.json')).json();
    for (const group of groups)
      for (const path of assetManifest[group]) {
        const response = await fetch(path);
        if (!response.ok)
          throw new Error('Cannot load ' + path + ': ' + response.status);
        // Sequential downloads bound staging to one file, rather than all assets.
        const bytes = new Uint8Array(await response.arrayBuffer());
        Module.FS.mkdirTree('/' + path.slice(0, path.lastIndexOf('/')));
        Module.FS.createDataFile('/' + path, null, bytes, true, false, true);
        assetPaths.push('/' + path);
        assetBytes += bytes.byteLength;
        assetDownloaded += bytes.byteLength;
      }
    Module._preview_assets_ready();
  } catch (error) {
    self.previewError(error.message);
  }
};
self.previewAssetsConsumed = () => {
  for (const path of assetPaths) Module.FS.unlink(path);
  assetPaths = [];
  assetBytes = 0;
};
self.previewControls = (controls) =>
  self.postMessage({ type: 'controls', controls });
function command(data) {
  if (data.type === 'resize') {
    if (data.logicalWidth)
      command({
        type: 'viewport',
        width: data.logicalWidth,
        height: data.logicalHeight,
      });
    return;
  }
  if (!ready) {
    pending.push(data);
    return;
  }
  if (data.type === 'quality')
    Module._preview_quality(data.preset ?? -1, data.height ?? 0);
  else if (data.type === 'viewport')
    Module._preview_viewport(data.width, data.height);
  else if (data.type === 'pointer')
    Module._preview_pointer(
      data.x,
      data.y,
      data.down ? 1 : 0,
      data.present ? 1 : 0,
    );
  else if (data.type === 'keys') Module._preview_keys(data.mask);
  else if (data.type === 'menu') Module._preview_menu(data.open ? 1 : 0);
  else if (data.type === 'pause') Module._preview_pause(data.value ? 1 : 0);
  else if (data.type === 'seek') Module._preview_seek(data.value);
  else if (data.type === 'visibility')
    Module._preview_visibility(data.value ? 1 : 0);
}
self.onmessage = async ({ data }) => {
  if (data.type !== 'start') {
    command(data);
    return;
  }
  canvas = data.canvas;
  canvas.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    self.previewError(
      'The graphics context was lost. Reload to restart the preview.',
    );
  });
  importScripts('gpu-memory.js');
  let context;
  let backend = data.options.backend || 'auto';
  try {
    if (!['auto', 'webgpu', 'webgl2'].includes(backend))
      throw new Error('Unknown graphics backend.');
    if (backend !== 'webgl2') {
      importScripts('graphics-data.js', 'webgpu-context.js');
      try {
        context = await createObservatoryGPU(canvas, self.previewError);
        backend = 'webgpu';
      } catch (error) {
        if (backend === 'webgpu' || !error.webgpuUnavailable) throw error;
        self.postMessage({
          type: 'status',
          message: 'WebGPU unavailable; using WebGL 2.',
        });
      }
    }
    if (!context) {
      context = canvas.getContext('webgl2', {
        alpha: false,
        antialias: false,
        depth: false,
        powerPreference: 'high-performance',
      });
      if (!context) throw new Error('WebGL 2 is unavailable.');
      ObservatoryGPUMemory.instrumentGL(context);
      backend = 'webgl2';
    }
    if (backend === 'webgpu' && data.options.benchmark === '1')
      throw new Error(
        'Synchronous benchmark mode requires backend=webgl2. WebGPU completion is asynchronous.',
      );
  } catch (error) {
    self.previewError(error.message);
    return;
  }
  const rendererName =
    backend === 'webgl2'
      ? context.getParameter(
          context.getExtension('WEBGL_debug_renderer_info')
            ?.UNMASKED_RENDERER_WEBGL || context.RENDERER,
        )
      : '';
  data.options.software =
    context.softwareAdapter ||
    /swiftshader|llvmpipe|softpipe/i.test(rendererName)
      ? '1'
      : '0';
  self.postMessage({ type: 'backend', backend });
  self.Module = {
    preinitializedWebGLContext: context,
    previewBackend: backend,
    canvas,
    previewOptions: data.options,
    mainScriptUrlOrBlob: new URL('observatory.js', self.location.href).href,
    preRun: [
      () => {
        // Register the transferred canvas with Emscripten's HTML5 target lookup.
        GL.offscreenCanvases.scene = canvas;
      },
    ],
    onRuntimeInitialized() {
      ready = true;
      command({ type: 'visibility', value: data.hidden });
      command({
        type: 'viewport',
        width: data.logicalWidth,
        height: data.logicalHeight,
      });
      pending.splice(0).forEach(command);
    },
    setStatus(message) {
      if (message) self.postMessage({ type: 'status', message });
    },
    printErr(message) {
      console.error(message);
    },
    onAbort(message) {
      self.previewError(String(message));
    },
  };
  try {
    importScripts('observatory.js');
  } catch (error) {
    console.error(error.stack);
    self.previewError(error.message);
  }
};
