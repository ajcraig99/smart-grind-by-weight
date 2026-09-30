// Controller state diagram (SVG). Phases and transitions are taken from
// src/controllers/grind_controller.cpp (update(), continue_from_purge(), stop_grind(), return_to_idle(),
// start_additional_pulse()) and src/controllers/weight_grind_strategy.cpp / time_grind_strategy.cpp.
// The diagram only presents the phase the firmware reports; it contains no controller logic.

export const PHASE_NAMES = [
  'IDLE', 'INITIALIZING', 'SETUP', 'TARING', 'TARE_CONFIRM', 'PREDICTIVE', 'PULSE_DECISION',
  'PULSE_EXECUTE', 'PULSE_SETTLING', 'FINAL_SETTLING', 'TIME_GRINDING', 'MANUAL_GRINDING',
  'TIME_ADDITIONAL_PULSE', 'COMPLETED', 'TIMEOUT', 'PRIME', 'PRIME_SETTLING', 'PURGE_CONFIRM',
];

const W = 124, H = 30, SW = 128, SH = 24;
const NODES = {
  IDLE: { x: 20, y: 120 },
  INITIALIZING: { x: 170, y: 40 },
  SETUP: { x: 320, y: 40 },
  TARING: { x: 470, y: 40 },
  TARE_CONFIRM: { x: 620, y: 40 },
  PRIME: { x: 620, y: 120 },
  PRIME_SETTLING: { x: 470, y: 120 },
  PURGE_CONFIRM: { x: 320, y: 120 },
  PREDICTIVE: { x: 470, y: 200 },
  PULSE_SETTLING: { x: 320, y: 200 },
  PULSE_DECISION: { x: 320, y: 280 },
  PULSE_EXECUTE: { x: 470, y: 280 },
  FINAL_SETTLING: { x: 170, y: 280 },
  COMPLETED: { x: 20, y: 280, kind: 'ok' },
  TIMEOUT: { x: 20, y: 360, kind: 'bad' },
  MANUAL_GRINDING: { x: 780, y: 170, small: true },
  TIME_GRINDING: { x: 780, y: 230, small: true },
  TIME_ADDITIONAL_PULSE: { x: 170, y: 360, small: true },
};
const ANY = { x: 320, y: 400, w: 300, h: 26 };

// Edges: points are absolute; `key` is "FROM>TO" used to mark transitions seen in the current session.
const EDGES = [
  { key: 'IDLE>INITIALIZING', pts: [[144, 132], [170, 58]], label: 'start', lx: 160, ly: 92 },
  { key: 'INITIALIZING>SETUP', pts: [[294, 55], [320, 55]], label: '', },
  { key: 'SETUP>TARING', pts: [[444, 55], [470, 55]], label: '' },
  { key: 'TARING>TARE_CONFIRM', pts: [[594, 55], [620, 55]] },
  { key: 'TARE_CONFIRM>PRIME', pts: [[682, 70], [682, 120]], label: 'first tare', lx: 688, ly: 98, anchor: 'start' },
  { key: 'PRIME>PRIME_SETTLING', pts: [[620, 135], [594, 135]] },
  { key: 'PRIME_SETTLING>PURGE_CONFIRM', pts: [[470, 140], [444, 140]] },
  { key: 'PRIME_SETTLING>PREDICTIVE', pts: [[532, 150], [532, 200]], label: 'prime mode / fresh', lx: 538, ly: 180, anchor: 'start' },
  { key: 'PURGE_CONFIRM>PREDICTIVE', pts: [[420, 150], [490, 200]], label: 'continue', lx: 440, ly: 184, anchor: 'end' },
  { key: 'PURGE_CONFIRM>TARING', pts: [[436, 122], [480, 70]], label: 're-tare', lx: 458, ly: 98, anchor: 'end' },
  { key: 'TARE_CONFIRM>PREDICTIVE', pts: [[744, 50], [754, 50], [754, 215], [594, 215]], label: 'resume after purge', lx: 768, ly: 135, anchor: 'end', rot: true },
  { key: 'PREDICTIVE>PULSE_SETTLING', pts: [[470, 215], [444, 215]] },
  { key: 'PULSE_SETTLING>PULSE_DECISION', pts: [[382, 230], [382, 280]] },
  { key: 'PULSE_DECISION>PULSE_EXECUTE', pts: [[444, 290], [470, 290]] },
  { key: 'PULSE_EXECUTE>PULSE_SETTLING', pts: [[500, 280], [436, 230]] },
  { key: 'PULSE_DECISION>FINAL_SETTLING', pts: [[320, 298], [294, 298]] },
  { key: 'FINAL_SETTLING>COMPLETED', pts: [[170, 298], [144, 298]] },
  { key: 'COMPLETED>IDLE', pts: [[82, 280], [82, 150]], label: 'UI ack', lx: 88, ly: 214, anchor: 'start' },
  { key: 'TIMEOUT>IDLE', pts: [[20, 375], [4, 375], [4, 135], [20, 135]], label: '', },
  { key: 'FINAL_SETTLING>TIMEOUT', pts: [[190, 310], [144, 368]] },
  { key: 'COMPLETED>TIME_ADDITIONAL_PULSE', both: true, pts: [[120, 310], [176, 362]], label: 'pulse', lx: 168, ly: 334, anchor: 'start' },
  { key: 'TIME_ADDITIONAL_PULSE>COMPLETED', pts: null },
  { key: 'SETUP>MANUAL_GRINDING', dashed: true, pts: [[382, 40], [382, 22], [940, 22], [940, 182], [908, 182]], label: 'manual / time mode', lx: 700, ly: 18 },
  { key: 'SETUP>TIME_GRINDING', dashed: true, pts: [[940, 182], [940, 242], [908, 242]] },
  { key: 'TIME_GRINDING>FINAL_SETTLING', pts: [[844, 254], [844, 338], [232, 338], [232, 310]], label: 'time elapsed', lx: 560, ly: 334 },
  { key: 'ANY>TIMEOUT', dashed: true, pts: [[320, 413], [80, 413], [80, 390]] },
];

// XML namespace identifier (not a network address); assembled so the page contains no URL literals.
const SVG_NS = ['http:', '', 'www.w3.org', '2000', 'svg'].join('/');
function el(tag, attrs, text) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (text != null) e.textContent = text;
  return e;
}

export class StateDiagram {
  constructor(container) {
    this.container = container;
    this.nodeEls = {};
    this.edgeEls = {};
    this.seenNodes = new Set();
    this.seenEdges = new Set();
    this.active = null;
    this.build();
  }

  build() {
    const svg = el('svg', { viewBox: '-14 0 980 440', role: 'img', 'aria-label': 'Grind controller state diagram' });
    const defs = el('defs', {});
    for (const [id, cls] of [['arr', 'arrow'], ['arr-on', 'arrow on']]) {
      const m = el('marker', { id, viewBox: '0 0 10 10', refX: '9', refY: '5', markerWidth: '7', markerHeight: '7', orient: 'auto-start-reverse' });
      m.appendChild(el('path', { d: 'M0,0 L10,5 L0,10 z', class: cls }));
      defs.appendChild(m);
    }
    svg.appendChild(defs);

    const eg = el('g', {});
    const lg = el('g', {});
    for (const e of EDGES) {
      if (!e.pts) continue;
      const path = el('path', {
        d: 'M' + e.pts.map((p) => p.join(',')).join(' L'),
        class: 'edge' + (e.dashed ? ' dashed' : ''),
        'marker-end': 'url(#arr)',
      });
      if (e.both) path.setAttribute('marker-start', 'url(#arr)');
      eg.appendChild(path);
      this.edgeEls[e.key] = path;
      if (e.label) {
        const t = el('text', { x: e.lx, y: e.ly, class: 'elabel', 'text-anchor': e.anchor || 'middle' }, e.label);
        if (e.rot) t.setAttribute('transform', `rotate(-90 ${e.lx} ${e.ly})`);
        lg.appendChild(t);
      }
    }
    svg.appendChild(eg);
    svg.appendChild(lg);

    for (const [name, n] of Object.entries(NODES)) {
      const w = n.small ? SW : W, h = n.small ? SH : H;
      const g = el('g', { class: 'node' + (n.small ? ' small' : '') + (n.kind ? ' ' + n.kind : ''), transform: `translate(${n.x},${n.y})` });
      g.appendChild(el('rect', { width: w, height: h, rx: 6 }));
      g.appendChild(el('text', { x: w / 2, y: h / 2 + 0.5, 'text-anchor': 'middle', 'dominant-baseline': 'middle' }, name));
      svg.appendChild(g);
      this.nodeEls[name] = g;
    }
    const any = el('g', { class: 'node any', transform: `translate(${ANY.x},${ANY.y})` });
    any.appendChild(el('rect', { width: ANY.w, height: ANY.h, rx: 13 }));
    any.appendChild(el('text', { x: ANY.w / 2, y: ANY.h / 2 + 0.5, 'text-anchor': 'middle', 'dominant-baseline': 'middle' }, 'any active phase: abort → TIMEOUT, user stop → IDLE'));
    svg.appendChild(any);
    this.anyEl = any;

    this.container.textContent = '';
    this.container.appendChild(svg);
  }

  // A new grind session starts: forget the visited trail.
  resetTrail() {
    this.seenNodes.clear();
    this.seenEdges.clear();
    this.refresh();
  }

  // Phase transition observed in the trace (from/to are GrindPhase numbers).
  onTransition(fromPhase, toPhase) {
    const from = PHASE_NAMES[fromPhase], to = PHASE_NAMES[toPhase];
    if (!from || !to) return;
    if (from === 'IDLE' && to !== 'IDLE') { this.seenNodes.clear(); this.seenEdges.clear(); }
    this.seenNodes.add(from);
    this.seenNodes.add(to);
    let key = from + '>' + to;
    if (key === 'TIME_ADDITIONAL_PULSE>COMPLETED') key = 'COMPLETED>TIME_ADDITIONAL_PULSE';
    if (!this.edgeEls[key]) key = to === 'TIMEOUT' ? 'ANY>TIMEOUT' : null;
    if (key) this.seenEdges.add(key);
  }

  setActive(name) {
    this.active = name;
    this.refresh();
  }

  refresh() {
    for (const [name, g] of Object.entries(this.nodeEls)) {
      g.classList.toggle('active', name === this.active);
      g.classList.toggle('seen', name !== this.active && this.seenNodes.has(name));
    }
    for (const [key, p] of Object.entries(this.edgeEls)) {
      const on = this.seenEdges.has(key);
      p.classList.toggle('on', on);
      p.setAttribute('marker-end', on ? 'url(#arr-on)' : 'url(#arr)');
      if (p.hasAttribute('marker-start')) p.setAttribute('marker-start', on ? 'url(#arr-on)' : 'url(#arr)');
    }
    this.anyEl.classList.toggle('seen', this.seenEdges.has('ANY>TIMEOUT'));
  }
}
