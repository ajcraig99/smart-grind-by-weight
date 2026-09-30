// Accumulates the firmware-side CSV trace (10 ms rows) for plotting and export.
// Only the columns the page plots are parsed into typed arrays; raw text is kept for CSV export.

const COLS = [
  't_ms', 'phase', 'relay_pin', 'relay_contact', 'motor_speed', 'flow_burr_gps', 'flow_cup_gps',
  'm_cup_g', 'scale_true_g', 'fw_weight_g', 'fw_flow_gps', 'fw_target_g', 'cup_present',
];
// Index of each wanted column within the firmware CSV row (from the header, resolved at runtime).
const CSV_NAMES = {
  t_ms: 't_ms', phase: 'phase', relay_pin: 'relay_pin', relay_contact: 'relay_contact',
  motor_speed: 'motor_speed', flow_burr_gps: 'flow_burr_gps', flow_cup_gps: 'flow_cup_gps',
  m_cup_g: 'm_cup_g', scale_true_g: 'scale_true_g', fw_weight_g: 'fw_weight_g',
  fw_flow_gps: 'fw_flow_gps', fw_target_g: 'fw_target_g', cup_present: 'cup_present',
};

export const MAX_ROWS = 240000; // ~40 min of virtual time at 10 ms

export class TraceStore {
  constructor() {
    this.header = '';
    this.colIndex = null;
    this.cap = 1 << 14;
    this.n = 0;
    this.cols = {};
    for (const c of COLS) this.cols[c] = new Float64Array(this.cap);
    this.chunks = []; // { text, rows }
    this.droppedRows = 0;
    this.carry = '';
    this.onPhaseChange = null; // (tSeconds, fromPhase, toPhase)
    this.lastPhase = -1;
  }

  clear() {
    this.n = 0;
    this.chunks = [];
    this.droppedRows = 0;
    this.carry = '';
    this.lastPhase = -1;
  }

  setHeader(h) {
    this.header = h.endsWith('\n') ? h : h + '\n';
    this.colIndex = null;
  }

  grow() {
    this.cap *= 2;
    for (const c of COLS) {
      const a = new Float64Array(this.cap);
      a.set(this.cols[c].subarray(0, this.n));
      this.cols[c] = a;
    }
  }

  resolveColumns(headerLine) {
    const names = headerLine.trim().split(',');
    this.colIndex = {};
    for (const c of COLS) this.colIndex[c] = names.indexOf(CSV_NAMES[c]);
  }

  // Append a chunk of raw CSV text (may start with a header line and may end mid-row).
  append(text) {
    if (!text) return 0;
    let data = this.carry + text;
    const lastNl = data.lastIndexOf('\n');
    if (lastNl < 0) { this.carry = data; return 0; }
    this.carry = data.slice(lastNl + 1);
    data = data.slice(0, lastNl + 1);
    const lines = data.split('\n');
    let rows = 0;
    const keep = [];
    for (const line of lines) {
      if (!line) continue;
      if (line.charCodeAt(0) === 116 /* 't' of t_ms */ && line.startsWith('t_ms')) {
        if (!this.header) this.header = line + '\n';
        this.resolveColumns(line);
        continue;
      }
      if (!this.colIndex) this.resolveColumns(this.header || 't_ms');
      const f = line.split(',');
      if (this.n >= this.cap) this.grow();
      const i = this.n;
      for (const c of COLS) {
        const idx = this.colIndex[c];
        this.cols[c][i] = idx >= 0 ? parseFloat(f[idx]) : NaN;
      }
      const ph = this.cols.phase[i];
      if (ph !== this.lastPhase) {
        if (this.onPhaseChange && this.lastPhase >= 0) this.onPhaseChange(this.cols.t_ms[i] / 1000, this.lastPhase, ph);
        this.lastPhase = ph;
      }
      this.n++;
      rows++;
      keep.push(line);
    }
    if (rows) this.chunks.push({ text: keep.join('\n') + '\n', rows });
    if (this.n > MAX_ROWS) this.trim();
    return rows;
  }

  trim() {
    const target = Math.floor(MAX_ROWS * 0.8);
    let drop = this.n - target;
    let dropped = 0;
    while (this.chunks.length > 1 && dropped + this.chunks[0].rows <= drop) {
      dropped += this.chunks[0].rows;
      this.chunks.shift();
    }
    if (dropped <= 0) return;
    for (const c of COLS) this.cols[c].copyWithin(0, dropped, this.n);
    this.n -= dropped;
    this.droppedRows += dropped;
  }

  get lastT() { return this.n ? this.cols.t_ms[this.n - 1] / 1000 : 0; }

  // First row index with time >= t (seconds).
  lowerBound(t) {
    const a = this.cols.t_ms;
    const tm = t * 1000;
    let lo = 0, hi = this.n;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (a[mid] < tm) lo = mid + 1; else hi = mid;
    }
    return lo;
  }

  // Index range of the most recent grind session (phase != IDLE), padded by `padS` seconds.
  lastSessionRange(padS = 1) {
    const ph = this.cols.phase;
    let end = -1;
    for (let i = this.n - 1; i >= 0; i--) { if (ph[i] !== 0) { end = i; break; } }
    if (end < 0) return null;
    let start = end;
    while (start > 0 && ph[start - 1] !== 0) start--;
    const tStart = this.cols.t_ms[start] / 1000 - padS;
    const active = end >= this.n - 2;
    const tEnd = active ? this.lastT : this.cols.t_ms[end] / 1000 + padS;
    return { t0: tStart, t1: Math.max(tEnd, tStart + 1) };
  }

  csv() {
    const parts = [this.header];
    for (const ch of this.chunks) parts.push(ch.text);
    return parts.join('');
  }
}
