'use strict';
const canvas = document.querySelector('#scene');
const status = document.querySelector('#status');
const loading = document.querySelector('#loading');
const pause = document.querySelector('#pause');
const performanceOutput = document.querySelector('#performance');
let isPaused = new URLSearchParams(location.search).get('paused') === '1';
let failed = false;
let rendererWorker;
const backend = document.querySelector('#backend');
const requestedBackend = new URLSearchParams(location.search).get('backend') || 'auto';
if (['auto', 'webgpu', 'webgl2'].includes(requestedBackend)) backend.value = requestedBackend;
window.previewError = message => {
  if (failed) return;
  failed = true;
  document.body.classList.remove('running');
  for (const id of ['debug-open', 'debug-hint', 'browser-controls']) document.querySelector('#' + id).hidden = true;
  rendererWorker?.terminate();
  document.querySelector('#launch').hidden = true;
  pause.disabled = true;
  loading.hidden = false;
  loading.querySelector('h2').textContent = 'Preview unavailable';
  status.textContent = message;
  document.querySelector('#help').textContent = 'Try a current browser with hardware acceleration enabled. The desktop renderer is available from the source link.';
};
const samples = [];
window.previewMetrics = { frames: 0, samples };
let previousFrame = 0;
window.previewFrame = frame => {
  if (failed) return;
  const now = performance.now();
  const interval = previousFrame ? now - previousFrame : 0;
  previousFrame = now;
  samples.push({ ...frame, interval });
  if (samples.length > 600) samples.shift();
  Object.assign(window.previewMetrics, frame, { frames: frame.count });
  if (!failed) loading.hidden = true;
  pause.disabled = false;
  document.body.classList.add('running');
  document.querySelector('#debug-open').hidden = false;
  if (!window.previewControls?.open && !hintDismissed) document.querySelector('#debug-hint').hidden = false;
  pause.textContent = isPaused ? 'Resume' : 'Pause';
  if (frame.paused) {
    performanceOutput.textContent = `Paused · ${frame.width} × ${frame.height} · 30 fps target`;
  } else if (frame.count === 1 || frame.count % 15 === 0) {
    const recent = samples.slice(-30).filter(item => item.interval > 0);
    const fps = recent.length ? recent.length * 1000 / recent.reduce((sum, item) => sum + item.interval, 0) : 0;
    performanceOutput.textContent = `${fps.toFixed(1)} fps · ${frame.width} × ${frame.height} · 30 fps target`;
  }
};
function setPause(value) {
  isPaused = value;
  previousFrame = 0;
  rendererWorker.postMessage({type: 'pause', value});
  pause.textContent = value ? 'Resume' : 'Pause';
}
pause.addEventListener('click', () => setPause(!isPaused));
const keyCodes = new Map([['Space', 1], ['Home', 2], ['ArrowLeft', 4], ['ArrowRight', 8]]);
let keyMask = 0;
for (const type of ['keydown', 'keyup']) document.addEventListener(type, event => {
  if (event.code === 'Escape' && type === 'keydown' && window.previewControls?.open) {
    rendererWorker?.postMessage({type: 'menu', open: false});
    return;
  }
  const bit = keyCodes.get(event.code);
  if (!bit || /^(INPUT|SELECT|BUTTON)$/.test(event.target.tagName)) return;
  if (window.previewControls?.open) event.preventDefault();
  keyMask = type === 'keydown' ? keyMask | bit : keyMask & ~bit;
  rendererWorker?.postMessage({type: 'keys', mask: keyMask});
});
let hintDismissed = false;
window.previewMenu = state => {
  window.previewControls = state;
  isPaused = !state.playing;
  pause.textContent = isPaused ? 'Resume' : 'Pause';
  document.querySelector('#debug-open').setAttribute('aria-expanded', String(state.open));
  document.querySelector('#browser-controls').hidden = !state.open;
  if (state.open) {
    hintDismissed = true;
    document.querySelector('#debug-hint').hidden = true;
  }
};
for (const id of ['debug-open', 'debug-hint']) document.querySelector('#' + id).addEventListener('click', () => rendererWorker?.postMessage({type: 'menu', open: true}));
document.querySelector('#debug-close').addEventListener('click', () => rendererWorker?.postMessage({type: 'menu', open: false}));
let pointer = {x: -1, y: -1, down: false, present: false};
let pendingMove = 0;
function sendPointer() { pendingMove = 0; rendererWorker?.postMessage({type: 'pointer', ...pointer}); }
for (const type of ['pointermove', 'pointerdown', 'pointerup', 'pointercancel']) document.addEventListener(type, event => {
  const target = event.target;
  const present = target === canvas || target.id === 'debug-open' || canvas.hasPointerCapture(event.pointerId);
  if (!present && !pointer.present && !pointer.down) return;
  const rect = canvas.getBoundingClientRect();
  pointer = {x: event.clientX - rect.left, y: event.clientY - rect.top,
    down: present && type !== 'pointercancel' && !!(event.buttons & 1), present};
  if (type === 'pointerdown' && target === canvas) canvas.setPointerCapture(event.pointerId);
  if (type === 'pointermove') {
    if (!pendingMove) pendingMove = requestAnimationFrame(sendPointer);
  } else {
    if (pendingMove) cancelAnimationFrame(pendingMove);
    sendPointer(); // Button edges retain their positions even during slow frames.
  }
});
window.addEventListener('blur', () => {
  pointer.down = pointer.present = false;
  sendPointer();
  keyMask = 0;
  rendererWorker?.postMessage({type: 'keys', mask: 0});
});
document.querySelector('#fullscreen').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.documentElement.requestFullscreen();
  } catch (error) { performanceOutput.textContent = error.message; }
});
function resize() {
  // Match viewport aspect without letterboxing or multiplying work on HiDPI.
  const logicalWidth = Math.max(1, window.innerWidth), logicalHeight = Math.max(1, window.innerHeight);
  const limit = Number(document.querySelector('#resolution').value);
  const pixels = limit * limit * 16 / 9;
  const scale = Math.min(limit / logicalHeight, Math.sqrt(pixels / (logicalWidth * logicalHeight)));
  const width = Math.max(1, Math.floor(logicalWidth * scale));
  const height = Math.max(1, Math.floor(logicalHeight * scale));
  if (rendererWorker) {
    rendererWorker.postMessage({type: 'resize', width, height, logicalWidth, logicalHeight});
  } else { canvas.width = width; canvas.height = height; }
}
document.querySelector('#resolution').addEventListener('change', resize);
let pendingResize = 0;
window.addEventListener('resize', () => {
  if (!pendingResize) pendingResize = requestAnimationFrame(() => { pendingResize = 0; resize(); });
});
resize();
document.addEventListener('visibilitychange', () => {
  rendererWorker?.postMessage({type: 'visibility', value: document.hidden});
});
document.querySelector('#stop').addEventListener('click', () => {
  rendererWorker?.terminate();
  location.reload();
});
async function start() {
  try {
    if (!window.isSecureContext || !('serviceWorker' in navigator))
      throw new Error('Use HTTPS (or localhost) in a browser with service worker support.');
    if (!crossOriginIsolated) {
      status.textContent = 'Preparing browser threads…';
      await navigator.serviceWorker.register('isolation-worker.js', { scope: './' });
      await navigator.serviceWorker.ready;
      if (!navigator.serviceWorker.controller) {
        await new Promise(resolve => navigator.serviceWorker.addEventListener('controllerchange', resolve, { once: true }));
      }
      if (sessionStorage.getItem('observatory-isolation-reload'))
        throw new Error('Browser isolation is unavailable. Allow service workers and reload the page.');
      sessionStorage.setItem('observatory-isolation-reload', '1');
      location.reload();
      return;
    }
    sessionStorage.removeItem('observatory-isolation-reload');
    if (typeof SharedArrayBuffer === 'undefined') throw new Error('This browser does not support WebAssembly threads.');
    status.textContent = 'Ready for flight';
    const launch = document.querySelector('#launch');
    launch.disabled = false;
    launch.addEventListener('click', () => {
      launch.hidden = true;
      backend.disabled = true;
      status.textContent = 'Downloading the observatory…';
      if (!canvas.transferControlToOffscreen) {
        window.previewError('This preview requires OffscreenCanvas support.');
        return;
      }
      rendererWorker = new Worker('render-worker.js');
      rendererWorker.onerror = event => window.previewError(event.message || 'The renderer worker stopped.');
      rendererWorker.onmessage = ({data}) => {
        if (data.type === 'frame') window.previewFrame(data.frame);
        else if (data.type === 'controls') window.previewMenu(data.controls);
        else if (data.type === 'error') window.previewError(data.message);
        else if (data.type === 'backend') {
          window.previewMetrics.backend = data.backend;
          document.querySelector('#backend-status').textContent = data.backend === 'webgpu' ? 'WebGPU' : 'WebGL 2';
        }
        else if (data.type === 'status' && !failed) status.textContent = data.message;
      };
      const offscreen = canvas.transferControlToOffscreen();
      rendererWorker.postMessage({type: 'start', canvas: offscreen,
        options: {...Object.fromEntries(new URLSearchParams(location.search)), backend: backend.value}, hidden: document.hidden,
        logicalWidth: window.innerWidth, logicalHeight: window.innerHeight}, [offscreen]);
    }, { once: true });
  } catch (error) { window.previewError(error.message); }
}
start();
