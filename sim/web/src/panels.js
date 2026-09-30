// DOM panels: masses, actions, faults, plant parameters, run history, log. Presentation only.
import { ACTIONS, FAULTS } from './twin.js';

const $ = (id) => document.getElementById(id);

export function download(filename, text, type = 'text/csv') {
  const blob = new Blob([text], { type: type + ';charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}

let toastTimer = 0;
export function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 3500);
}

// ---------------------------------------------------------------- masses
const MASSES = [
  ['hopper', 'm_hopper_g', 'Hopper'],
  ['burr', 'm_burr_g', 'Burrs'],
  ['chute', 'm_chute_g', 'Chute'],
  ['inflight', 'm_inflight_g', 'In flight'],
  ['cup', 'm_cup_g', 'Cup'],
  ['platform', 'm_platform_g', 'Platform'],
  ['spilled', 'm_spilled_g', 'Spilled'],
];

export function buildMasses() {
  const box = $('mass-bars');
  box.textContent = '';
  const refs = {};
  for (const [id, key, label] of MASSES) {
    const row = document.createElement('div');
    row.className = 'mbar';
    row.innerHTML = `<span>${label}</span><div class="track"><div class="fill"></div></div><span class="val">0.000</span>`;
    box.appendChild(row);
    refs[key] = { fill: row.querySelector('.fill'), val: row.querySelector('.val') };
  }
  return refs;
}

export function updateMasses(refs, s) {
  const total = Math.max(1, s.m_loaded_g, 25);
  for (const [, key] of MASSES) {
    const v = s[key] ?? 0;
    refs[key].fill.style.width = Math.min(100, (Math.max(0, v) / total) * 100).toFixed(1) + '%';
    refs[key].val.textContent = v.toFixed(3);
  }
  const e = s.conservation_error_g;
  const c = $('cons-err');
  c.textContent = (e >= 0 ? '+' : '') + e.toExponential(2) + ' g';
  c.className = 'mono ' + (Math.abs(e) < 1e-6 ? 'ok' : 'bad');
}

// ---------------------------------------------------------------- actions and faults
export function buildActions(twinGetter, defaults) {
  const box = $('actions');
  box.textContent = '';
  const inputs = {};
  const add = (label, action, opt) => {
    const item = document.createElement('div');
    item.className = 'item';
    const b = document.createElement('button');
    b.textContent = label;
    b.title = opt?.title || label;
    let inp = null;
    if (opt?.input) {
      inp = document.createElement('input');
      inp.type = 'number';
      inp.step = 'any';
      inp.value = opt.input.value ?? '';
      inp.placeholder = opt.input.placeholder || '';
      inp.setAttribute('aria-label', label + ' value');
      inputs[opt.id] = inp;
    }
    b.addEventListener('click', () => {
      const t = twinGetter();
      if (!t) return;
      let v = opt?.fixed ?? 0;
      if (inp) v = inp.value === '' ? 0 : parseFloat(inp.value);
      if (opt?.handler) opt.handler(t, v); else t.action(action, v);
    });
    item.appendChild(b);
    if (inp) {
      item.appendChild(inp);
      const u = document.createElement('span');
      u.className = 'unit';
      u.textContent = 'g';
      item.appendChild(u);
    }
    box.appendChild(item);
    return { b, inp };
  };
  add('Place cup', ACTIONS.PLACE_CUP, { id: 'cup', title: 'Put the cup on the platform (empty cup mass; blank = default)', input: { placeholder: 'auto' } });
  add('Remove cup', ACTIONS.REMOVE_CUP, { title: 'Lift the cup and its contents off the platform' });
  add('Empty cup', ACTIONS.EMPTY_CUP, { title: 'Tip the cup contents out (cup must be off the platform)' });
  add('Load beans', ACTIONS.LOAD_BEANS, { id: 'beans', title: 'Drop beans into the hopper', input: { value: defaults.beans } });
  add('Bump scale', ACTIONS.BUMP, { id: 'bump', title: 'Knock the platform (peak transient force)', input: { value: 5 } });
  let pressing = false;
  let pressBtn = null;
  const setPress = (on) => {
    pressing = on;
    pressBtn.textContent = on ? 'Release press' : 'Press on scale';
    pressBtn.classList.toggle('on', on);
  };
  const press = add('Press on scale', ACTIONS.PRESS, {
    id: 'press',
    title: 'Rest a hand on the platform (steady extra mass); press again to release',
    input: { value: 200 },
    handler: (t, v) => { t.action(ACTIONS.PRESS, pressing ? 0 : v); setPress(!pressing); },
  });
  pressBtn = press.b;
  add('Clean chute', ACTIONS.CLEAN_CHUTE, { title: 'Remove grounds retained in the chute' });
  add('Wipe platform', ACTIONS.WIPE_PLATFORM, { title: 'Wipe grounds off the bare platform' });
  return { inputs, resetPress() { setPress(false); } };
}

export const FAULT_DEFS = [
  { code: FAULTS.LC_DISCONNECT, id: 'lc_disconnect', label: 'Load cell disconnect' },
  { code: FAULTS.LC_STUCK, id: 'lc_stuck', label: 'Load cell stuck' },
  { code: FAULTS.LC_NOISE_BURST, id: 'lc_noise', label: 'Noise burst', input: 20, unit: '×' },
  { code: FAULTS.RELAY_STUCK_ON, id: 'relay_on', label: 'Relay stuck on' },
  { code: FAULTS.RELAY_STUCK_OFF, id: 'relay_off', label: 'Relay stuck off' },
  { code: FAULTS.MOTOR_STALL, id: 'motor_stall', label: 'Motor stall' },
  { code: FAULTS.FEED_BLOCK, id: 'feed_block', label: 'Bean feed blocked' },
];

export function buildFaults(twinGetter) {
  const box = $('faults');
  box.textContent = '';
  const active = new Set();
  const buttons = {};
  for (const f of FAULT_DEFS) {
    const item = document.createElement('div');
    item.className = 'item';
    const b = document.createElement('button');
    b.textContent = f.label;
    b.setAttribute('aria-pressed', 'false');
    b.dataset.fault = f.id;
    let inp = null;
    if (f.input != null) {
      inp = document.createElement('input');
      inp.type = 'number';
      inp.step = 'any';
      inp.value = f.input;
      inp.setAttribute('aria-label', f.label + ' multiplier');
    }
    b.addEventListener('click', () => {
      const t = twinGetter();
      if (!t) return;
      const on = !active.has(f.code);
      t.fault(f.code, on, inp ? parseFloat(inp.value) || 1 : 0);
      if (on) active.add(f.code); else active.delete(f.code);
      b.classList.toggle('active-fault', on);
      b.setAttribute('aria-pressed', on ? 'true' : 'false');
    });
    item.appendChild(b);
    if (inp) {
      item.appendChild(inp);
      const u = document.createElement('span');
      u.className = 'unit';
      u.textContent = f.unit;
      item.appendChild(u);
    }
    box.appendChild(item);
    buttons[f.code] = b;
  }
  return {
    active,
    clear() {
      active.clear();
      for (const b of Object.values(buttons)) { b.classList.remove('active-fault'); b.setAttribute('aria-pressed', 'false'); }
    },
  };
}

// ---------------------------------------------------------------- plant parameters
function fmtParam(v, min, max) {
  const span = Math.abs(max - min) || 1;
  const d = span >= 100 ? 1 : span >= 10 ? 2 : span >= 1 ? 3 : span >= 0.1 ? 4 : 6;
  return String(+v.toFixed(d));
}

export function buildParams(table, onChange) {
  const box = $('params');
  box.textContent = '';
  const rows = {};
  for (const p of table) {
    const row = document.createElement('div');
    row.className = 'param';
    row.dataset.name = p.name;
    const sourced = p.source && p.source !== 'placeholder';
    const span = p.max - p.min;
    const pct = span > 0 ? ((p.default - p.min) / span) * 100 : 0;
    row.innerHTML = `
      <div class="pname"><code></code><span class="pbadge ${sourced ? 'src' : 'ph'}" title=""></span><span class="u"></span></div>
      <div class="pctl">
        <div class="sliderwrap"><input type="range" aria-label=""><span class="defmark" title="default"></span></div>
        <input type="number" step="any" aria-label="">
        <button title="Reset to default" aria-label="Reset to default">↺</button>
      </div>
      <div class="pdesc"></div>`;
    row.querySelector('code').textContent = p.name;
    const badge = row.querySelector('.pbadge');
    badge.textContent = sourced ? 'sourced' : 'placeholder';
    badge.title = sourced ? p.source : 'placeholder (see sim/ASSUMPTIONS.md)';
    row.querySelector('.u').textContent = p.unit;
    row.querySelector('.pdesc').textContent = p.description + (sourced ? '  [' + p.source + ']' : '');
    const range = row.querySelector('input[type=range]');
    range.min = p.min; range.max = p.max; range.step = span / 1000; range.value = p.default;
    range.setAttribute('aria-label', p.name);
    row.querySelector('.defmark').style.left = `calc(${pct}% + ${(0.5 - pct / 100) * 14}px)`;
    const num = row.querySelector('input[type=number]');
    num.value = fmtParam(p.default, p.min, p.max);
    num.setAttribute('aria-label', p.name + ' value');
    const reset = row.querySelector('button');
    const apply = (v, fromSlider) => {
      if (!Number.isFinite(v)) return;
      range.value = v;
      if (fromSlider) num.value = fmtParam(v, p.min, p.max);
      row.classList.toggle('changed', Math.abs(v - p.default) > 1e-12);
      onChange(p.name, v, p.default);
    };
    range.addEventListener('input', () => apply(parseFloat(range.value), true));
    num.addEventListener('change', () => apply(parseFloat(num.value), false));
    reset.addEventListener('click', () => { num.value = fmtParam(p.default, p.min, p.max); apply(p.default, false); });
    box.appendChild(row);
    rows[p.name] = { row, range, num, p };
  }
  return {
    rows,
    resetAll() { for (const r of Object.values(rows)) r.row.querySelector('button').click(); },
    filter(q) {
      q = q.trim().toLowerCase();
      for (const r of Object.values(rows)) {
        const hay = (r.p.name + ' ' + r.p.description + ' ' + r.p.unit).toLowerCase();
        r.row.style.display = !q || hay.includes(q) ? '' : 'none';
      }
    },
    // Reflect values already living in the plant (after a restart import).
    setValue(name, v) {
      const r = rows[name];
      if (!r) return;
      r.range.value = v;
      r.num.value = fmtParam(v, r.p.min, r.p.max);
      r.row.classList.toggle('changed', Math.abs(v - r.p.default) > 1e-12);
    },
  };
}

// ---------------------------------------------------------------- history
const HISTORY_COLS = [
  'world', 'index', 't_start_s', 't_end_s', 'terminal', 'result', 'error', 'target_g', 'fw_final_g',
  'true_cup_g', 'true_error_g', 'pulses', 'motor_on_s', 'motor_invalid_signal_s', 'motor_after_end_s', 'open',
];

export function trueCup(r) {
  return r.true_cup_g != null ? r.true_cup_g : r.true_cup_end_g;
}

export function historyCsv(rows) {
  const lines = [HISTORY_COLS.join(',')];
  for (const r of rows) {
    const tc = trueCup(r);
    const rec = { ...r, true_cup_g: tc, true_error_g: tc != null && r.target_g != null ? tc - r.target_g : '' };
    lines.push(HISTORY_COLS.map((c) => {
      const v = rec[c];
      if (v == null) return '';
      if (typeof v === 'string') return '"' + v.replace(/"/g, '""') + '"';
      return String(v);
    }).join(','));
  }
  return lines.join('\n') + '\n';
}

const f3 = (v) => (v == null ? '-' : v.toFixed(3));
const f2 = (v) => (v == null ? '-' : v.toFixed(2));

export function renderHistory(rows, multiWorld) {
  const tb = document.querySelector('#history tbody');
  const frag = document.createDocumentFragment();
  for (const r of rows.slice().reverse()) {
    const tr = document.createElement('tr');
    const tc = trueCup(r);
    const open = r.open === 1;
    const err = r.result && tc != null && r.target_g != null ? tc - r.target_g : null;
    const res = r.result || (r.abandoned ? 'interrupted' : open ? 'running' : '-');
    const resCls = r.result === 'SUCCESS' ? 'res-SUCCESS' : !r.result || open ? 'res-open' : 'res-fail';
    const eCls = err == null ? '' : Math.abs(err) <= 0.03 ? 'e-ok' : Math.abs(err) <= 0.1 ? 'e-warn' : 'e-bad';
    const id = (multiWorld ? `w${r.world}.` : '') + (r.index + 1);
    tr.innerHTML = `<td>${id}</td><td class="${resCls}">${res}${r.terminal && r.terminal !== 'COMPLETED' ? ' (' + r.terminal + ')' : ''}</td>` +
      `<td>${r.error || ''}</td><td>${f2(r.target_g)}</td><td>${f2(r.fw_final_g)}</td>` +
      `<td class="${r.true_cup_g == null ? 'prov' : ''}" title="${r.true_cup_g == null ? 'not yet closed: value at end of grind' : 'after the cup is settled'}">${f3(tc)}</td>` +
      `<td class="${eCls}">${err == null ? '-' : (err >= 0 ? '+' : '') + err.toFixed(3)}</td><td>${r.pulses}</td><td>${f2(r.motor_on_s)}</td><td>${f2(r.t_start_s)}</td>`;
    frag.appendChild(tr);
  }
  tb.textContent = '';
  tb.appendChild(frag);
  $('history-empty').hidden = rows.length > 0;
}

// ---------------------------------------------------------------- log
export class LogPanel {
  constructor() {
    this.lines = [];
    this.pending = '';
    this.dirty = true;
    this.max = 20000;
    this.shown = 600;
  }
  append(text) {
    if (!text) return;
    const s = this.pending + text;
    const parts = s.split('\n');
    this.pending = parts.pop();
    for (const p of parts) this.lines.push(p);
    if (this.lines.length > this.max) this.lines.splice(0, this.lines.length - this.max);
    this.dirty = true;
  }
  clear() { this.lines = []; this.pending = ''; this.dirty = true; }
  text() { return this.lines.join('\n') + (this.pending ? '\n' + this.pending : '') + '\n'; }
  render(filter, follow) {
    if (!this.dirty) return;
    this.dirty = false;
    const q = filter.trim().toLowerCase();
    const out = [];
    for (let i = this.lines.length - 1; i >= 0 && out.length < this.shown; i--) {
      const l = this.lines[i];
      if (!q || l.toLowerCase().includes(q)) out.push(l);
    }
    out.reverse();
    const pre = $('log');
    pre.textContent = out.join('\n');
    if (follow) pre.scrollTop = pre.scrollHeight;
  }
}
