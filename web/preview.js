'use strict';
const canvas = document.querySelector('#scene');
const status = document.querySelector('#status');
const loading = document.querySelector('#loading');
const pause = document.querySelector('#pause');
const time = document.querySelector('#time');
const performanceOutput = document.querySelector('#performance');
let isPaused = new URLSearchParams(location.search).get('paused') === '1';
let failed = false;
let rendererWorker;
window.previewError = message => {
  failed = true;
  rendererWorker?.terminate();
  document.querySelector('#launch').hidden = true;
  pause.disabled = time.disabled = true;
  loading.hidden = false;
  loading.querySelector('h2').textContent = 'Preview unavailable';
  status.textContent = message;
  document.querySelector('#help').textContent = 'Try a current browser with hardware acceleration enabled. The desktop renderer is available from the source link.';
};
const samples = [];
window.previewMetrics = { frames: 0, samples };
let previousFrame = 0;
window.previewFrame = frame => {
  const now = performance.now();
  const interval = previousFrame ? now - previousFrame : 0;
  previousFrame = now;
  samples.push({ ...frame, interval });
  if (samples.length > 600) samples.shift();
  Object.assign(window.previewMetrics, frame, { frames: frame.count });
  if (!failed) loading.hidden = true;
  pause.disabled = time.disabled = false;
  pause.textContent = isPaused ? 'Resume' : 'Pause';
  if (document.activeElement !== time) time.value = ((frame.time % 1800) + 1800) % 1800;
  if (frame.count % 15 === 0) {
    const recent = samples.slice(-30).filter(item => item.interval > 0);
    const fps = recent.length * 1000 / recent.reduce((sum, item) => sum + item.interval, 0);
    performanceOutput.textContent = `${fps.toFixed(1)} fps · ${frame.width} × ${frame.height} · 30 fps target`;
  }
};
function setPause(value) {
  isPaused = value;
  rendererWorker.postMessage({type: 'pause', value});
  pause.textContent = value ? 'Resume' : 'Pause';
}
pause.addEventListener('click', () => setPause(!isPaused));
time.addEventListener('change', () => {
  const current = window.previewMetrics.time;
  rendererWorker.postMessage({type: 'seek', value: current - current % 1800 + Number(time.value)});
});
document.addEventListener('keydown', event => {
  if (event.code === 'Space' && event.target === document.body && !pause.disabled) {
    event.preventDefault();
    setPause(!isPaused);
  }
});
document.querySelector('#fullscreen').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.documentElement.requestFullscreen();
  } catch (error) { performanceOutput.textContent = error.message; }
});
function resize() {
  // Explicit pixel budget: HiDPI/fullscreen never silently multiplies GPU work.
  const height = Number(document.querySelector('#resolution').value);
  const width = Math.round(height * 16 / 9);
  if (rendererWorker) rendererWorker.postMessage({type: 'resize', width, height});
  else { canvas.width = width; canvas.height = height; }
}
document.querySelector('#resolution').addEventListener('change', resize);
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
      status.textContent = 'Downloading the observatory…';
      if (!canvas.transferControlToOffscreen) {
        window.previewError('This preview requires OffscreenCanvas support.');
        return;
      }
      rendererWorker = new Worker('render-worker.js');
      rendererWorker.onerror = event => window.previewError(event.message || 'The renderer worker stopped.');
      rendererWorker.onmessage = ({data}) => {
        if (data.type === 'frame') window.previewFrame(data.frame);
        else if (data.type === 'error') window.previewError(data.message);
        else if (data.type === 'status' && !failed) status.textContent = data.message;
      };
      const offscreen = canvas.transferControlToOffscreen();
      rendererWorker.postMessage({type: 'start', canvas: offscreen,
        options: Object.fromEntries(new URLSearchParams(location.search)), hidden: document.hidden}, [offscreen]);
      document.querySelector('#stop').hidden = false;
    }, { once: true });
  } catch (error) { window.previewError(error.message); }
}
start();
