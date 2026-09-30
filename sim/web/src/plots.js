// Canvas time-series plots over the TraceStore. No chart library; min/max decimation per pixel column.

export const COLORS = {
  grid: '#2a3140',
  axis: '#8a93a6',
  text: '#c9d1e0',
  fw: '#ffb454',      // firmware reading
  truth: '#4fd1c5',   // simulated ground truth
  burr: '#b794f4',
  target: '#ff6b6b',
  band: 'rgba(255,107,107,0.22)',
  relay: '#ff6b6b',
  contact: '#f6e05e',
  motor: '#63b3ed',
};

function niceStep(range, targetTicks) {
  const raw = range / Math.max(1, targetTicks);
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  const f = raw / p;
  const nice = f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10;
  return nice * p;
}

function fmt(v, step) {
  const d = step >= 1 ? 0 : Math.min(4, Math.ceil(-Math.log10(step)));
  return v.toFixed(d);
}

export class Plot {
  // opts: { title, unit, series: [{label,color,fn(cols,i),width,dash}], y: {mode:'auto'|'fixed', min,max, include:[...]},
  //         band: {center(cols,i), half}, strip: bool }
  constructor(canvas, opts) {
    this.canvas = canvas;
    this.opts = opts;
    this.ctx = canvas.getContext('2d');
    this.hoverX = null;
    this.dpr = 1;
    this.last = null;
    const ro = new ResizeObserver(() => this.resize());
    ro.observe(canvas);
    canvas.addEventListener('mousemove', (e) => {
      const r = canvas.getBoundingClientRect();
      this.hoverX = e.clientX - r.left;
      this.redraw();
    });
    canvas.addEventListener('mouseleave', () => { this.hoverX = null; this.redraw(); });
    this.resize();
  }

  resize() {
    const r = this.canvas.getBoundingClientRect();
    this.dpr = window.devicePixelRatio || 1;
    const w = Math.max(10, Math.round(r.width * this.dpr));
    const h = Math.max(10, Math.round(r.height * this.dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
    this.redraw();
  }

  redraw() { if (this.last) this.draw(this.last.store, this.last.t0, this.last.t1); }

  draw(store, t0, t1) {
    this.last = { store, t0, t1 };
    const { ctx, canvas, dpr } = this;
    const W = canvas.width / dpr;
    const H = canvas.height / dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    const pad = { l: 52, r: 10, t: 18, b: 18 };
    const pw = W - pad.l - pad.r;
    const ph = H - pad.t - pad.b;
    if (pw < 20 || ph < 10) return;

    const cols = store.cols;
    const i0 = store.lowerBound(t0);
    const i1 = Math.min(store.n, store.lowerBound(t1) + 1);
    const tx = (t) => pad.l + ((t - t0) / (t1 - t0)) * pw;

    // x grid
    ctx.font = '11px system-ui, sans-serif';
    ctx.textBaseline = 'top';
    ctx.textAlign = 'center';
    const xs = niceStep(t1 - t0, Math.max(3, pw / 90));
    ctx.strokeStyle = COLORS.grid;
    ctx.fillStyle = COLORS.axis;
    ctx.lineWidth = 1;
    for (let t = Math.ceil(t0 / xs) * xs; t <= t1 + 1e-9; t += xs) {
      const x = Math.round(tx(t)) + 0.5;
      ctx.beginPath(); ctx.moveTo(x, pad.t); ctx.lineTo(x, pad.t + ph); ctx.stroke();
      ctx.fillText(fmt(t, xs) + ' s', x, pad.t + ph + 3);
    }

    if (this.opts.strip) { this.drawStrip(store, t0, t1, i0, i1, pad, pw, ph, W); return; }

    // y range
    const yo = this.opts.y || { mode: 'auto' };
    let ymin = Infinity, ymax = -Infinity;
    if (yo.mode === 'fixed') { ymin = yo.min; ymax = yo.max; }
    else {
      for (const s of this.opts.series) {
        if (s.noRange) continue;
        for (let i = i0; i < i1; i++) {
          const v = s.fn(cols, i);
          if (v === v && isFinite(v)) { if (v < ymin) ymin = v; if (v > ymax) ymax = v; }
        }
      }
      for (const v of (yo.include || [])) { if (v < ymin) ymin = v; if (v > ymax) ymax = v; }
      if (!isFinite(ymin)) { ymin = 0; ymax = 1; }
      if (yo.floor !== undefined && ymin > yo.floor) ymin = yo.floor;
      if (ymax - ymin < (yo.minSpan || 1e-3)) { const m = (ymax + ymin) / 2; ymin = m - (yo.minSpan || 1e-3) / 2; ymax = m + (yo.minSpan || 1e-3) / 2; }
      const padY = (ymax - ymin) * 0.06;
      ymin -= padY; ymax += padY;
    }
    const ty = (v) => pad.t + ph - ((v - ymin) / (ymax - ymin)) * ph;

    // y grid
    const ys = niceStep(ymax - ymin, Math.max(2, ph / 40));
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let v = Math.ceil(ymin / ys) * ys; v <= ymax + 1e-9; v += ys) {
      const y = Math.round(ty(v)) + 0.5;
      ctx.strokeStyle = v === 0 ? '#3a4356' : COLORS.grid;
      ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(pad.l + pw, y); ctx.stroke();
      ctx.fillStyle = COLORS.axis;
      ctx.fillText(fmt(v, ys), pad.l - 6, y);
    }

    ctx.save();
    ctx.beginPath();
    ctx.rect(pad.l, pad.t, pw, ph);
    ctx.clip();

    // tolerance band around a per-row centre
    const band = this.opts.band;
    if (band) {
      ctx.fillStyle = COLORS.band;
      let started = false;
      const upper = [];
      const lower = [];
      const step = Math.max(1, Math.floor((i1 - i0) / (pw * 2)));
      for (let i = i0; i < i1; i += step) {
        const c = band.center(cols, i);
        if (!(c === c) || !isFinite(c)) { started = false; this.fillBand(ctx, upper, lower); upper.length = 0; lower.length = 0; continue; }
        started = true;
        const x = tx(cols.t_ms[i] / 1000);
        let yu = ty(c + band.half);
        let yl = ty(c - band.half);
        if (yl - yu < 3) { const m = (yl + yu) / 2; yu = m - 1.5; yl = m + 1.5; }
        upper.push([x, yu]);
        lower.push([x, yl]);
      }
      if (started) this.fillBand(ctx, upper, lower);
    }

    for (const s of this.opts.series) this.drawSeries(ctx, s, cols, i0, i1, tx, ty, pw);
    ctx.restore();

    // legend + title
    this.drawLegend(ctx, pad, yo.mode === 'fixed' ? `${this.opts.title} (${this.opts.unit})` : `${this.opts.title} (${this.opts.unit})`);
    this.drawHover(ctx, store, t0, t1, i0, i1, pad, pw, ph, W, tx);
  }

  fillBand(ctx, upper, lower) {
    if (upper.length < 2) return;
    ctx.beginPath();
    ctx.moveTo(upper[0][0], upper[0][1]);
    for (const p of upper) ctx.lineTo(p[0], p[1]);
    for (let k = lower.length - 1; k >= 0; k--) ctx.lineTo(lower[k][0], lower[k][1]);
    ctx.closePath();
    ctx.fill();
  }

  drawSeries(ctx, s, cols, i0, i1, tx, ty, pw) {
    ctx.strokeStyle = s.color;
    ctx.lineWidth = s.width || 1.4;
    ctx.setLineDash(s.dash || []);
    ctx.lineJoin = 'round';
    ctx.beginPath();
    const m = i1 - i0;
    let pen = false;
    const pt = (i, v) => {
      const x = tx(cols.t_ms[i] / 1000);
      const y = ty(v);
      if (!pen) { ctx.moveTo(x, y); pen = true; } else ctx.lineTo(x, y);
    };
    if (m <= pw * 2) {
      for (let i = i0; i < i1; i++) {
        const v = s.fn(cols, i);
        if (!(v === v) || !isFinite(v)) { pen = false; continue; }
        pt(i, v);
      }
    } else {
      const per = m / pw;
      for (let k = 0; k < pw; k++) {
        const a = i0 + Math.floor(k * per);
        const b = Math.min(i1, i0 + Math.floor((k + 1) * per) + 1);
        let lo = Infinity, hi = -Infinity, il = -1, ih = -1;
        for (let i = a; i < b; i++) {
          const v = s.fn(cols, i);
          if (!(v === v) || !isFinite(v)) continue;
          if (v < lo) { lo = v; il = i; }
          if (v > hi) { hi = v; ih = i; }
        }
        if (il < 0) { pen = false; continue; }
        if (il <= ih) { pt(il, lo); if (ih !== il) pt(ih, hi); } else { pt(ih, hi); pt(il, lo); }
      }
    }
    ctx.stroke();
    ctx.setLineDash([]);
  }

  drawLegend(ctx, pad, title) {
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.font = '11px system-ui, sans-serif';
    ctx.fillStyle = COLORS.text;
    let x = pad.l;
    ctx.fillText(title, x, 2);
    x += ctx.measureText(title).width + 14;
    for (const s of this.opts.series) {
      if (!s.label) continue;
      ctx.strokeStyle = s.color;
      ctx.lineWidth = 2;
      ctx.setLineDash(s.dash || []);
      ctx.beginPath(); ctx.moveTo(x, 8); ctx.lineTo(x + 14, 8); ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = COLORS.axis;
      ctx.fillText(s.label, x + 18, 2);
      x += 18 + ctx.measureText(s.label).width + 12;
    }
    if (this.opts.band) {
      ctx.fillStyle = COLORS.band;
      ctx.fillRect(x, 3, 14, 9);
      ctx.fillStyle = COLORS.axis;
      ctx.fillText(this.opts.band.label || 'tolerance', x + 18, 2);
    }
  }

  drawHover(ctx, store, t0, t1, i0, i1, pad, pw, ph, W, tx) {
    if (this.hoverX == null || this.hoverX < pad.l || this.hoverX > pad.l + pw) return;
    const t = t0 + ((this.hoverX - pad.l) / pw) * (t1 - t0);
    let i = store.lowerBound(t);
    if (i >= store.n) i = store.n - 1;
    if (i < 0) return;
    const x = tx(store.cols.t_ms[i] / 1000);
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(Math.round(x) + 0.5, pad.t); ctx.lineTo(Math.round(x) + 0.5, pad.t + ph); ctx.stroke();
    const lines = [`t = ${(store.cols.t_ms[i] / 1000).toFixed(2)} s`];
    for (const s of (this.opts.strip ? this.opts.lanes : this.opts.series)) {
      if (!s.label) continue;
      const v = s.fn(store.cols, i);
      lines.push(`${s.label}: ${v === v && isFinite(v) ? v.toFixed(s.digits ?? 3) : '-'}`);
    }
    ctx.font = '11px system-ui, sans-serif';
    const w = Math.max(...lines.map((l) => ctx.measureText(l).width)) + 10;
    const h = lines.length * 14 + 6;
    let bx = x + 10;
    if (bx + w > W - 2) bx = x - 10 - w;
    ctx.fillStyle = 'rgba(15,18,26,0.92)';
    ctx.fillRect(bx, pad.t + 4, w, h);
    ctx.strokeStyle = '#3a4356';
    ctx.strokeRect(bx + 0.5, pad.t + 4.5, w, h);
    ctx.fillStyle = COLORS.text;
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    lines.forEach((l, k) => ctx.fillText(l, bx + 5, pad.t + 7 + k * 14));
  }

  // Three lanes: relay command, relay contact (digital), motor speed (analog 0..1).
  drawStrip(store, t0, t1, i0, i1, pad, pw, ph, W) {
    const { ctx } = this;
    const cols = store.cols;
    const lanes = this.opts.lanes;
    const laneH = ph / lanes.length;
    const tx = (t) => pad.l + ((t - t0) / (t1 - t0)) * pw;
    ctx.save();
    ctx.beginPath(); ctx.rect(pad.l, pad.t, pw, ph); ctx.clip();
    lanes.forEach((ln, k) => {
      const top = pad.t + k * laneH + 3;
      const h = laneH - 6;
      ctx.fillStyle = 'rgba(255,255,255,0.03)';
      ctx.fillRect(pad.l, top, pw, h);
      ctx.fillStyle = ln.color;
      ctx.strokeStyle = ln.color;
      ctx.globalAlpha = ln.analog ? 0.85 : 0.9;
      // bucket per pixel: max value in bucket
      const m = Math.max(1, i1 - i0);
      const per = Math.max(1, m / pw);
      const n = Math.ceil(m / per);
      ctx.beginPath();
      if (ln.analog) ctx.moveTo(pad.l, top + h);
      for (let b = 0; b < n; b++) {
        const a = i0 + Math.floor(b * per);
        const e = Math.min(i1, i0 + Math.floor((b + 1) * per) + 1);
        let v = 0;
        for (let i = a; i < e; i++) { const x = ln.fn(cols, i); if (x === x && x > v) v = x; }
        const x = tx(cols.t_ms[Math.min(a, store.n - 1)] / 1000);
        const y = top + h - Math.min(1, Math.max(0, v)) * h;
        if (ln.analog) ctx.lineTo(x, y);
        else if (v > 0.5) ctx.fillRect(x, top + 2, Math.max(1, pw / n + 0.5), h - 4);
      }
      if (ln.analog) { ctx.lineTo(pad.l + pw, top + h); ctx.closePath(); ctx.globalAlpha = 0.35; ctx.fill(); ctx.globalAlpha = 1; ctx.lineWidth = 1.2; ctx.stroke(); }
      ctx.globalAlpha = 1;
    });
    ctx.restore();
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    ctx.font = '10px system-ui, sans-serif';
    lanes.forEach((ln, k) => {
      ctx.fillStyle = ln.color;
      ctx.fillText(ln.short, pad.l - 6, pad.t + k * laneH + laneH / 2);
    });
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.font = '11px system-ui, sans-serif';
    ctx.fillStyle = COLORS.text;
    ctx.fillText(this.opts.title, pad.l, 2);
    this.drawHover(ctx, store, t0, t1, i0, i1, pad, pw, ph, W, tx);
  }
}

export function makePlots(els) {
  const C = COLORS;
  const weight = new Plot(els.weight, {
    title: 'Weight', unit: 'g',
    series: [
      { label: 'firmware weight', color: C.fw, noRange: true, fn: (c, i) => c.fw_weight_g[i] },
      { label: 'true cup mass', color: C.truth, fn: (c, i) => c.m_cup_g[i] },
      { label: 'target', color: C.target, dash: [5, 4], width: 1.2, fn: (c, i) => (c.fw_target_g[i] > 0 ? c.fw_target_g[i] : NaN) },
    ],
    band: { center: (c, i) => (c.fw_target_g[i] > 0 ? c.fw_target_g[i] : NaN), half: 0.03, label: 'target ±0.03 g' },
    y: { mode: 'auto', floor: 0, minSpan: 1 },
  });
  const error = new Plot(els.error, {
    title: 'Error vs target', unit: 'g',
    series: [
      { label: 'firmware', color: C.fw, fn: (c, i) => (c.fw_target_g[i] > 0 ? c.fw_weight_g[i] - c.fw_target_g[i] : NaN) },
      { label: 'true', color: C.truth, fn: (c, i) => (c.fw_target_g[i] > 0 ? c.m_cup_g[i] - c.fw_target_g[i] : NaN) },
    ],
    band: { center: () => 0, half: 0.03, label: '±0.03 g tolerance' },
    y: { mode: 'fixed', min: -0.3, max: 0.3 },
  });
  const flow = new Plot(els.flow, {
    title: 'Flow', unit: 'g/s',
    series: [
      { label: 'firmware (200 ms)', color: C.fw, noRange: true, fn: (c, i) => c.fw_flow_gps[i] },
      { label: 'true at cup', color: C.truth, fn: (c, i) => c.flow_cup_gps[i] },
      { label: 'true at burrs', color: C.burr, dash: [4, 3], noRange: true, fn: (c, i) => c.flow_burr_gps[i] },
    ],
    y: { mode: 'auto', include: [0, 2.5], minSpan: 1 },
  });
  const strip = new Plot(els.strip, {
    title: 'Relay and motor', strip: true,
    lanes: [
      { short: 'cmd', label: 'relay command', color: C.relay, fn: (c, i) => c.relay_pin[i], digits: 0 },
      { short: 'contact', label: 'relay contact', color: C.contact, fn: (c, i) => c.relay_contact[i], digits: 0 },
      { short: 'motor', label: 'motor speed', color: C.motor, analog: true, fn: (c, i) => c.motor_speed[i], digits: 2 },
    ],
  });
  return [weight, error, flow, strip];
}
