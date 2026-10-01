// Page controller: render loop, virtual-time scheduling, input forwarding, panel updates.
// All grind logic runs inside the WASM (the real firmware). This file only presents state and forwards inputs.
import { createTwin, PANEL_W, PANEL_H } from './twin.js';
import { TraceStore } from './trace.js';
import { makePlots } from './plots.js';
import { StateDiagram, PHASE_NAMES } from './diagram.js';
import { createScene3D } from './scene3d.js';
import {
  buildMasses, updateMasses, buildActions, buildFaults, buildParams, renderHistory, historyCsv,
  LogPanel, download, toast,
} from './panels.js';

const $ = (id) => document.getElementById(id);
const SPEEDS = [0.25, 0.5, 1, 2, 5, 10, 20];
const FAST_BOOT_MS = 6500;
const TAP_MIN_HOLD_MS = 70; // firmware polls touch every 16 ms; it needs >= 50 ms for a tap
const PAUSED_TAP_EXTRA_MS = 250;
// esp_reset_reason_t codes reported by the twin
const RESET_REASONS = { 1: 'power-on', 3: 'software restart', 4: 'panic', 5: 'interrupt watchdog', 6: 'task watchdog', 7: 'other watchdog' };

const app = {
  twin: null,
  busy: true,
  world: 0,
  epoch: 0,
  used: { seed: 1, target: 18 },
  speed: 1,
  paused: false,
  budget: 0,
  vtRun: 0,
  lastFrame: performance.now(),
  lastPoll: 0,
  lastTrace: 0,
  lastStatT: performance.now(),
  stat: { v: 0, w: 0 },
  ratio: null,
  needRestart: false,
  touch: null,
  view: 'window',
  state: null,
  trace: new TraceStore(),
  log: new LogPanel(),
  archive: [],       // history rows from earlier module instances
  epochRows: [],     // rows of the running module instance
  historyKey: '',
  trail: [],
  trailDirty: true,
  params: new Map(), // name -> value for parameters that differ from the default
  paramTable: [],
  perf: { advance: 0, trace: 0, plots: 0, poll: 0, screen: 0, three: 0 },
  gui: {},
  three: { visible: false, collapsed: false },
};

// ------------------------------------------------------------------ helpers
const numVal = (id, dflt) => { const v = parseFloat($(id).value); return Number.isFinite(v) ? v : dflt; };

function scenarioJson() {
  return JSON.stringify({
    name: 'browser',
    target_g: numVal('in-target', 18),
    beans_g: numVal('in-beans', 22),
    purge_mode: 1,
    purge_action: $('in-purge').value,
  });
}

function setStatus(text) { $('world-status').textContent = text; }

// ------------------------------------------------------------------ screen
const screenCanvas = $('screen');
const sctx = screenCanvas.getContext('2d', { alpha: false });
const screenImage = sctx.createImageData(PANEL_W, PANEL_H);
const screenPixels = new Uint32Array(screenImage.data.buffer);
sctx.fillStyle = '#000';
sctx.fillRect(0, 0, PANEL_W, PANEL_H);

function refreshScreen() {
  if (app.twin && app.twin.takeFramebuffer(screenPixels)) {
    sctx.putImageData(screenImage, 0, 0);
    if (app.gui.scene3d) app.gui.scene3d.markScreenDirty();
  }
}

function panelXY(ev) {
  const r = screenCanvas.getBoundingClientRect();
  const x = ((ev.clientX - r.left) * PANEL_W) / r.width;
  const y = ((ev.clientY - r.top) * PANEL_H) / r.height;
  return [Math.min(PANEL_W - 1, Math.max(0, x)), Math.min(PANEL_H - 1, Math.max(0, y))];
}

// Touch input shared by the 2D canvas and the 3D screen (raycast). Coordinates are panel pixels.
function touchStart(x, y, id) {
  if (!app.twin || app.busy || app.touch) return false;
  app.touch = { id, x, y, pressVt: app.vtRun, releasing: false, releaseAt: 0 };
  app.twin.touch(x, y, true);
  return true;
}
function touchMove(x, y, id) {
  const t = app.touch;
  if (!t || t.id !== id || t.releasing || !app.twin) return;
  t.x = x; t.y = y;
  app.twin.touch(x, y, true);
}
function touchEnd(x, y, id) {
  const t = app.touch;
  if (!t || t.id !== id || t.releasing || !app.twin) return;
  t.x = x; t.y = y;
  t.releasing = true;
  t.releaseAt = Math.max(app.vtRun, t.pressVt + TAP_MIN_HOLD_MS);
  if (app.paused) {
    // The firmware only sees the tap while time advances: give it just enough.
    advance(t.releaseAt - app.vtRun + PAUSED_TAP_EXTRA_MS);
    pumpAll(true);
  }
}
screenCanvas.addEventListener('pointerdown', (ev) => {
  const [x, y] = panelXY(ev);
  if (touchStart(x, y, ev.pointerId)) {
    ev.preventDefault();
    screenCanvas.setPointerCapture(ev.pointerId);
  }
});
screenCanvas.addEventListener('pointermove', (ev) => { const [x, y] = panelXY(ev); touchMove(x, y, ev.pointerId); });
const endPointer = (ev) => { const [x, y] = panelXY(ev); touchEnd(x, y, ev.pointerId); };
screenCanvas.addEventListener('pointerup', endPointer);
screenCanvas.addEventListener('pointercancel', endPointer);
screenCanvas.addEventListener('contextmenu', (e) => e.preventDefault());

// ------------------------------------------------------------------ virtual time
// Synchronously run `ms` of virtual time in short chunks so touch releases land on time.
function advance(ms) {
  let left = Math.round(ms);
  const tw = app.twin;
  while (left > 0 && tw === app.twin && app.twin) {
    let chunk = Math.min(left, 50);
    const t = app.touch;
    if (t && t.releasing) chunk = Math.max(1, Math.min(chunk, t.releaseAt - app.vtRun));
    app.twin.runMs(chunk);
    app.vtRun += chunk;
    left -= chunk;
    if (t && t.releasing && app.vtRun >= t.releaseAt) {
      app.twin.touch(t.x, t.y, false);
      app.touch = null;
    }
    if (app.twin.restartRequested()) { app.needRestart = true; break; }
  }
}

function frame(now) {
  requestAnimationFrame(frame);
  const rawDt = now - app.lastFrame;
  const dt = Math.min(250, rawDt);
  app.lastFrame = now;
  if (!app.twin || app.busy) return;

  if (!app.paused) {
    app.budget += dt * app.speed;
    const start = performance.now();
    // Normally about 11 ms of work per frame; when frames are slow anyway (weak GPU, big window) use more of them.
    const maxWork = Math.min(60, Math.max(11, dt * 0.5));
    let ran = 0;
    while (app.budget >= 1 && performance.now() - start < maxWork && !app.needRestart) {
      const ms = Math.min(Math.floor(app.budget), 100);
      const pa = performance.now();
      advance(ms);
      app.perf.advance += performance.now() - pa;
      app.budget -= ms;
      ran += ms;
    }
    // If the machine cannot keep up, drop the backlog instead of spiralling.
    const cap = Math.max(50, 2 * dt * app.speed);
    if (app.budget > cap) app.budget = cap;
    app.stat.v += ran;
    app.stat.w += rawDt;
    if (now - app.lastStatT >= 500) {
      app.ratio = app.stat.w > 0 ? app.stat.v / app.stat.w : null;
      app.stat = { v: 0, w: 0 };
      app.lastStatT = now;
    }
  }
  pumpAll(false, now);
  frame3d(now);
  if (app.needRestart) powerCycle('firmware requested a restart');
}

// 3D twin: driven by the state snapshot each frame while the card is visible.
function frame3d(now) {
  const sc = app.gui.scene3d;
  if (!sc || !app.three.visible || !app.twin || app.busy || document.hidden) return;
  const t = performance.now();
  sc.frame(now, app.twin.state(), app.speed, app.paused);
  app.perf.three += performance.now() - t;
}

// Drain outputs and refresh panels. `force` ignores the rate limits.
function pumpAll(force, now = performance.now()) {
  if (!app.twin) return;
  let t = performance.now();
  refreshScreen();
  app.perf.screen += performance.now() - t;
  if (force || now - app.lastTrace >= 60) {
    app.lastTrace = now;
    t = performance.now();
    drainTrace();
    app.perf.trace += performance.now() - t;
    t = performance.now();
    drawPlots();
    app.perf.plots += performance.now() - t;
  }
  if (force || now - app.lastPoll >= 100) {
    app.lastPoll = now;
    t = performance.now();
    poll();
    app.perf.poll += performance.now() - t;
  }
}

function drainTrace() {
  const tw = app.twin;
  if (!tw) return;
  const rows = app.trace.append(tw.readTrace());
  const logText = tw.readLog();
  if (logText) app.log.append(logText);
  return rows;
}

function drawPlots() {
  const tr = app.trace;
  let t1 = Math.max(tr.lastT, 60);
  let t0 = t1 - 60;
  if (app.view === 'grind') {
    const r = tr.lastSessionRange(1);
    if (r) { t0 = r.t0; t1 = r.t1; }
    else { t0 = 0; t1 = Math.max(tr.lastT, 10); }
  }
  for (const p of app.gui.plots) p.draw(tr, t0, t1);
}

// ------------------------------------------------------------------ polling (<= 10 Hz DOM updates)
function allHistory() {
  const rows = [...app.archive, ...app.epochRows];
  const perWorld = new Map();
  return rows.map((r) => {
    const n = (perWorld.get(r.world) || 0) + 1;
    perWorld.set(r.world, n);
    return { ...r, index: n - 1 };
  });
}

function refreshHistory(force) {
  const tw = app.twin;
  if (!tw) return;
  let cur;
  try { cur = tw.records(); } catch { return; }
  const key = JSON.stringify(cur) + '|' + app.archive.length + '|' + app.world;
  if (!force && key === app.historyKey) return;
  app.historyKey = key;
  app.epochRows = cur.map((r) => ({ ...r, world: app.world, epoch: app.epoch }));
  const rows = allHistory();
  const multi = rows.some((r) => r.world !== rows[0].world);
  renderHistory(rows, multi);
}

function archiveEpoch() {
  try { refreshHistory(true); } catch { /* ignore */ }
  app.archive.push(...app.epochRows.map((r) => (r.open === 1 ? { ...r, abandoned: true } : r)));
  app.epochRows = [];
}


function poll() {
  const tw = app.twin;
  if (!tw) return;
  const s = tw.state();
  app.state = s;
  $('vt').textContent = s.t_s.toFixed(3);
  const r = $('ratio');
  if (app.paused) r.textContent = 'paused';
  else if (app.ratio != null) {
    r.textContent = `×${app.ratio.toFixed(app.ratio < 10 ? 2 : 1)} achieved`;
    r.style.color = app.ratio < app.speed * 0.9 ? 'var(--warn)' : '';
  }
  $('kv-phase').textContent = s.phase_name;
  $('kv-ui').textContent = s.ui_state_name;
  $('kv-operator').textContent = s.operator;
  const flags = [];
  if (s.fw_scale_fault) flags.push('scale fault ' + s.fw_scale_fault);
  if (s.fw_safety_stop) flags.push('SAFETY STOP');
  if (s.motor_stalled) flags.push('motor stalled');
  $('kv-fw').textContent =
    `${s.fw_weight_g.toFixed(2)} g, ${s.fw_flow_gps.toFixed(2)} g/s` +
    (s.fw_target_g > 0 ? `, target ${s.fw_target_g.toFixed(2)} g` : '') +
    (s.fw_latency_ms > 0 ? `, latency ${s.fw_latency_ms.toFixed(0)} ms` : '') +
    (flags.length ? ' [' + flags.join(', ') + ']' : '');
  $('diag-ui').textContent = 'UI: ' + s.ui_state_name;
  app.gui.diagram.setActive(s.phase_name);

  // panel brightness / power
  const b = Math.max(0.06, Math.min(1, s.brightness));
  screenCanvas.style.filter = b < 0.999 ? `brightness(${b.toFixed(2)})` : '';
  $('screen-off').hidden = !!s.display_on;

  updateMasses(app.gui.masses, s);
  refreshHistory(false);
  app.log.render($('log-filter').value, $('log-follow').checked);
  renderTrail();

  $('btn-auto').disabled = app.busy || !s.booted || tw.operatorActive();
  $('led-cmd').classList.toggle('on', !!s.relay_pin);
  $('led-contact').classList.toggle('on', !!s.relay_contact);
  $('btn-powercycle').disabled = app.busy || !s.booted;
  $('btn-step').disabled = !app.paused;
  if (s.restart) app.needRestart = true;

  const w = `World ${app.world}, seed ${app.used.seed}, target ${app.used.target} g`;
  setStatus(!s.booted ? w + ' - booting firmware' : w + (app.paused ? ' - paused' : ` - running at ${app.speed}×`));
}

function renderTrail() {
  if (!app.trailDirty) return;
  app.trailDirty = false;
  const last = app.trail.slice(-8);
  $('trail').innerHTML = last.length
    ? last.map((e) => `${e.t.toFixed(2)} s &nbsp;${e.from} &rarr; <b>${e.to}</b>`).join('<br>')
    : 'No phase transitions yet.';
}

// ------------------------------------------------------------------ world management
async function startWorld({ blob = null, reason = '', twin = null } = {}) {
  app.busy = true;
  app.needRestart = false;
  app.touch = null;
  const fresh = !blob;
  setStatus(fresh ? 'Creating world...' : 'Power-cycling firmware...');
  const tw = twin || (await createTwin());
  const seed = Math.max(0, Math.floor(numVal('in-seed', 1)));
  const target = numVal('in-target', 18);

  if (fresh) {
    app.world += 1;
    app.used = { seed, target };
    app.trace.clear();
    app.trail = [];
    app.trailDirty = true;
    app.gui.faults.clear();
    app.gui.actions.resetPress();
    app.gui.diagram.resetTrail();
    app.log.append(`--- world ${app.world}: seed ${seed}, target ${target} g ---\n`);
  } else {
    app.log.append(`--- power cycle (${reason}) ---\n`);
  }
  app.epoch += 1;
  app.epochRows = [];
  app.historyKey = '';

  tw.init(seed, target, scenarioJson());
  if (fresh) {
    for (const [name, v] of app.params) tw.setParam(name, v);
    app.trace.setHeader(tw.traceHeader());
  } else {
    const rc = tw.importPersist(blob);
    if (rc !== 0) app.log.append(`[page] persist import failed (${rc})\n`);
    tw.traceSuppressHeader();
    // The plant parameters travelled with the blob; reflect them in the sliders.
    for (const p of app.paramTable) {
      const v = tw.getParam(p.name);
      if (Number.isFinite(v)) app.gui.params.setValue(p.name, v);
    }
  }
  tw.boot();
  app.twin = tw;
  app.vtRun = 0;
  app.budget = 0;
  app.ratio = null;
  app.stat = { v: 0, w: 0 };
  if (fresh) advance(FAST_BOOT_MS);
  app.busy = false;
  if (fresh && $('in-startpaused').checked) setPaused(true);
  pumpAll(true);
  $('btn-boot').classList.remove('pending');
  $('btn-boot').textContent = 'Boot / Restart world';
}

async function bootWorld() {
  if (app.twin) { archiveEpoch(); app.twin = null; }
  await startWorld();
}

async function powerCycle(reason) {
  if (app.busy || !app.twin) return;
  app.busy = true;
  const old = app.twin;
  try {
    drainTrace();
    archiveEpoch();
    const why = RESET_REASONS[old.restartReason()] || 'code ' + old.restartReason();
    if (app.needRestart) reason += ' [' + why + ']';
    const blob = old.exportPersist();
    app.twin = null;
    await startWorld({ blob, reason });
  } catch (e) {
    console.error(e);
    toast('Power cycle failed: ' + e.message);
    app.busy = false;
  }
}

// ------------------------------------------------------------------ UI wiring
function buildSpeed() {
  const seg = $('speed-seg');
  for (const sp of SPEEDS) {
    const b = document.createElement('button');
    b.textContent = sp + '×';
    b.dataset.speed = sp;
    b.setAttribute('role', 'radio');
    const on = sp === app.speed;
    b.classList.toggle('on', on);
    b.setAttribute('aria-checked', on ? 'true' : 'false');
    b.addEventListener('click', () => setSpeed(sp));
    seg.appendChild(b);
  }
}

function setSpeed(sp) {
  app.speed = sp;
  app.ratio = null;
  app.stat = { v: 0, w: 0 };
  for (const b of $('speed-seg').children) {
    const on = parseFloat(b.dataset.speed) === sp;
    b.classList.toggle('on', on);
    b.setAttribute('aria-checked', on ? 'true' : 'false');
  }
}

function setPaused(p) {
  app.paused = p;
  $('btn-pause').textContent = p ? 'Resume' : 'Pause';
  $('btn-pause').classList.toggle('on', p);
  $('btn-step').disabled = !p;
  app.budget = 0;
  if (app.twin) pumpAll(true);
}

function wire() {
  buildSpeed();
  $('btn-pause').addEventListener('click', () => setPaused(!app.paused));
  $('btn-step').addEventListener('click', () => {
    if (!app.twin || app.busy || !app.paused) return;
    advance(20);
    pumpAll(true);
  });
  $('btn-boot').addEventListener('click', () => { if (!app.busy) bootWorld().catch(fatal); });
  $('btn-powercycle').addEventListener('click', () => powerCycle('operator request'));
  $('btn-auto').addEventListener('click', () => {
    const tw = app.twin;
    if (!tw || app.busy) return;
    tw.setScenario(scenarioJson()); // dose and purge action apply to this run
    tw.operatorStart();
    $('btn-auto').disabled = true;
    pumpAll(true);
  });

  for (const id of ['in-seed', 'in-target']) {
    $(id).addEventListener('change', () => {
      const pending = numVal('in-seed', 1) !== app.used.seed || numVal('in-target', 18) !== app.used.target;
      $('btn-boot').classList.toggle('pending', pending);
      $('btn-boot').textContent = pending ? 'Restart world to apply *' : 'Boot / Restart world';
    });
  }

  for (const b of $('scale-seg').querySelectorAll('button')) {
    b.addEventListener('click', () => {
      $('screen-wrap').classList.toggle('scale15', b.dataset.scale === '1.5');
      $('card-screen').classList.toggle('big', b.dataset.scale === '1.5');
      for (const o of $('scale-seg').querySelectorAll('button')) {
        const on = o === b;
        o.classList.toggle('on', on);
        o.setAttribute('aria-checked', on ? 'true' : 'false');
      }
    });
  }
  for (const b of $('view-seg').querySelectorAll('button')) {
    b.addEventListener('click', () => {
      app.view = b.dataset.view;
      for (const o of $('view-seg').querySelectorAll('button')) {
        const on = o === b;
        o.classList.toggle('on', on);
        o.setAttribute('aria-checked', on ? 'true' : 'false');
      }
      drawPlots();
    });
  }

  $('btn-trace-csv').addEventListener('click', () => {
    const csv = app.trace.csv();
    if (app.trace.droppedRows) toast(`Oldest ${app.trace.droppedRows} rows were dropped to bound memory.`);
    download(`grind-twin-trace-seed${app.used.seed}.csv`, csv);
  });
  $('btn-history-csv').addEventListener('click', () => download('grind-twin-history.csv', historyCsv(allHistory())));
  $('btn-log-txt').addEventListener('click', () => download('grind-twin-log.txt', app.log.text(), 'text/plain'));
  $('btn-log-clear').addEventListener('click', () => { app.log.clear(); app.log.render('', true); });
  $('log-filter').addEventListener('input', () => { app.log.dirty = true; app.log.render($('log-filter').value, $('log-follow').checked); });
  $('param-filter').addEventListener('input', () => app.gui.params.filter($('param-filter').value));
  $('btn-params-reset').addEventListener('click', () => app.gui.params.resetAll());
  window.addEventListener('resize', () => drawPlots());
  const hdr = document.querySelector('.topbar');
  const setH = () => document.documentElement.style.setProperty('--header-h', hdr.offsetHeight + 'px');
  new ResizeObserver(setH).observe(hdr);
  setH();
}

function wire3d() {
  const wrap = $('three-wrap');
  app.gui.scene3d = createScene3D(wrap, screenCanvas, {
    down: (x, y, id) => touchStart(x, y, id),
    move: (x, y, id) => touchMove(x, y, id),
    up: (x, y, id) => touchEnd(x, y, id),
  });
  const card = $('card-3d');
  const sc = app.gui.scene3d;
  if (!sc) {
    $('three-tools').hidden = true;
    $('btn-3d-toggle').hidden = true;
    $('card-3d').querySelector('.three-hint').hidden = true;
    return;
  }
  new IntersectionObserver((entries) => {
    app.three.visible = entries.some((e) => e.isIntersecting) && !app.three.collapsed;
    if (app.three.visible) sc.wake();
  }).observe(wrap);
  $('btn-explode').addEventListener('click', () => {
    const on = !sc.exploded;
    sc.setExploded(on);
    $('btn-explode').classList.toggle('on', on);
    $('btn-explode').setAttribute('aria-pressed', on ? 'true' : 'false');
  });
  $('btn-view-reset').addEventListener('click', () => sc.resetView());
  $('btn-3d-toggle').addEventListener('click', () => {
    app.three.collapsed = !app.three.collapsed;
    card.classList.toggle('collapsed', app.three.collapsed);
    $('btn-3d-toggle').textContent = app.three.collapsed ? 'Show 3D' : 'Hide 3D';
    app.three.visible = !app.three.collapsed;
    if (app.three.visible) sc.wake();
  });
}

function fatal(e) {
  console.error(e);
  setStatus('Error: ' + (e && e.message ? e.message : e));
  app.busy = false;
}

async function main() {
  app.gui.plots = makePlots({
    weight: $('plot-weight'), error: $('plot-error'), flow: $('plot-flow'), strip: $('plot-strip'),
  });
  app.gui.diagram = new StateDiagram($('diagram'));
  app.gui.masses = buildMasses();
  app.gui.faults = buildFaults(() => app.twin);
  app.gui.actions = buildActions(() => app.twin, { beans: numVal('in-beans', 22) });
  wire();
  wire3d();

  app.trace.onPhaseChange = (t, from, to) => {
    app.gui.diagram.onTransition(from, to);
    app.trail.push({ t, from: PHASE_NAMES[from] || from, to: PHASE_NAMES[to] || to });
    if (app.trail.length > 500) app.trail.splice(0, 250);
    app.trailDirty = true;
  };

  // The parameter table is static, but a module instance is needed to read it.
  const first = await createTwin();
  app.paramTable = first.paramTable();
  app.gui.params = buildParams(app.paramTable, (name, v, dflt) => {
    if (Math.abs(v - dflt) > 1e-12) app.params.set(name, v); else app.params.delete(name);
    if (app.twin) app.twin.setParam(name, v);
  });
  await startWorld({ twin: first });
  requestAnimationFrame(frame);
}

// Small read-only surface for the headless tests.
window.twinApp = {
  state: () => app.state,
  history: () => allHistory(),
  get paused() { return app.paused; },
  get speed() { return app.speed; },
  get busy() { return app.busy; },
  get world() { return app.world; },
  traceRows: () => app.trace.n,
  perf: () => ({ ...app.perf }),
  get scene3d() { return app.gui.scene3d; },
  // Layout regression hooks: drive the firmware UI and audit what LVGL laid out.
  ui: (cmd, arg) => app.twin.ui(cmd, arg),
  layoutAudit: () => app.twin.layoutAudit(),
};

main().catch(fatal);
