'use strict';
// Only browser hosting lives here. Simulation, meshes and rendering stay in C++.
let canvas;
let ready = false;
const pending = [];
self.previewError = message => self.postMessage({type: 'error', message});
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
self.onmessage = ({data}) => {
  if (data.type !== 'start') { command(data); return; }
  canvas = data.canvas;
  canvas.addEventListener('webglcontextlost', event => {
    event.preventDefault();
    self.previewError('The graphics context was lost. Reload to restart the preview.');
  });
  self.Module = {
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
  catch (error) { self.previewError(error.message); }
};
