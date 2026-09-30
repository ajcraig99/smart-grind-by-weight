// Shared helpers for the visual QA suite: page launch with problem tracking, waiting helpers,
// PNG analysis (done in a scratch browser page, so no image library is needed) and the in-page
// "freeze on terminal phase" watcher.
import { chromium } from 'playwright-core';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const here = path.dirname(fileURLToPath(import.meta.url));
export const OUT = path.join(here, 'out');
export const SHOTS = path.join(here, 'screenshots');
export const PAGE_FILE = path.resolve(here, '..', 'dist', 'index.html');
export const PAGE_URL = pathToFileURL(PAGE_FILE).href; // file:///.../sim/dist/index.html
export const CHROME = process.env.CHROME_PATH || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome';

export const PANEL_W = 280;
export const PANEL_H = 456;

// Default: --disable-gpu (as in sim/web/test/run.mjs). With software WebGL (SwiftShader) the whole page
// composites about 5x slower in this VM, so only the 3D scenario uses it (launch(true)).
export const LAUNCH_ARGS = ['--no-sandbox', '--disable-gpu'];
export const LAUNCH_ARGS_GL = ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'];

export async function launch(gl = false) {
  return chromium.launch({ executablePath: CHROME, headless: true, args: gl ? LAUNCH_ARGS_GL : LAUNCH_ARGS });
}

// ------------------------------------------------------------------------------------------
// Result bookkeeping
export class Report {
  constructor() {
    this.checks = [];   // {vp, scn, name, ok, severity, detail, repro}
    this.notes = [];    // {vp, scn, text}
    this.values = {};   // deterministic values, keyed vp/scn/name
    this.numbers = {};  // free-form measurements
    this.shots = [];    // file names written
  }
  check(vp, scn, name, ok, detail = '', severity = 'major', repro = '') {
    const c = { vp, scn, name, ok: !!ok, severity, detail: String(detail), repro };
    this.checks.push(c);
    console.log(`${c.ok ? 'PASS' : 'FAIL'} [${vp}] ${scn}: ${name}${detail ? '  (' + detail + ')' : ''}${c.ok ? '' : '  <' + severity + '>'}`);
    return c.ok;
  }
  note(vp, scn, text) {
    this.notes.push({ vp, scn, text });
    console.log(`NOTE [${vp}] ${scn}: ${text}`);
  }
  value(vp, scn, key, v) { (this.values[`${vp}/${scn}`] ||= {})[key] = v; }
}

// ------------------------------------------------------------------------------------------
// A scenario session: a fresh page (fresh WASM instance), problem capture, fixed-seed world.
export class Session {
  constructor(browser, rep, vp, scn, repro) {
    this.browser = browser; this.rep = rep; this.vp = vp; this.scn = scn; this.repro = repro;
    this.errors = []; this.warnings = []; this.requests = [];
  }
  async open(size) {
    this.ctx = await this.browser.newContext({ viewport: size, acceptDownloads: true });
    this.page = await this.ctx.newPage();
    const p = this.page;
    p.on('console', (m) => {
      if (m.type() === 'error') this.errors.push('console.error: ' + m.text());
      else if (m.type() === 'warning' && !/willReadFrequently/.test(m.text())) this.warnings.push('console.warning: ' + m.text());
    });
    p.on('pageerror', (e) => this.errors.push('pageerror: ' + e.message));
    p.on('requestfailed', (r) => this.errors.push('requestfailed: ' + r.url().slice(0, 120)));
    p.on('request', (r) => this.requests.push(r.url()));
    await p.goto(PAGE_URL);
    await this.ready();
    return this;
  }
  get tag() { return `${this.vp}_${this.scn}`; }
  state() { return this.page.evaluate(() => window.twinApp.state()); }
  ready(timeout = 60000) {
    return this.page.waitForFunction(() => window.twinApp && !window.twinApp.busy && window.twinApp.state() && window.twinApp.state().booted,
      null, { timeout });
  }
  // Restart into a fresh world with the given inputs; with startPaused the world is held at t=6.5 s
  // so scripted runs start at exactly the same virtual time (deterministic).
  async freshWorld({ seed = 1, target = 18, beans = 22, purge = 'discard', startPaused = true } = {}) {
    const p = this.page;
    const w0 = await p.evaluate(() => window.twinApp.world);
    await p.fill('#in-seed', String(seed));
    await p.fill('#in-target', String(target));
    await p.fill('#in-beans', String(beans));
    await p.selectOption('#in-purge', purge);
    await (startPaused ? p.check('#in-startpaused') : p.uncheck('#in-startpaused'));
    await p.click('#btn-boot');
    await p.waitForFunction((w) => window.twinApp.world === w + 1 && !window.twinApp.busy, w0, { timeout: 60000 });
    await this.ready();
  }
  setSpeed(sp) { return this.page.click(`#speed-seg button[data-speed="${sp}"]`); }
  pauseToggle() { return this.page.click('#btn-pause'); }
  waitPhase(names, timeout = 120000) {
    const list = Array.isArray(names) ? names : [names];
    return this.page.waitForFunction((l) => { const s = window.twinApp.state(); return s && l.includes(s.phase_name); },
      list, { timeout, polling: 'raf' });
  }
  waitPhaseNot(name, timeout = 60000) {
    return this.page.waitForFunction((n) => { const s = window.twinApp.state(); return s && s.phase_name !== n; },
      name, { timeout, polling: 'raf' });
  }
  waitVirtual(tAbs, timeout = 60000) {
    return this.page.waitForFunction((t) => window.twinApp.state().t_s >= t, tAbs, { timeout, polling: 'raf' });
  }
  waitClosed(n = 1, timeout = 180000) {
    return this.page.waitForFunction((k) => window.twinApp.history().filter((r) => r.open === 0 && r.result).length >= k,
      n, { timeout, polling: 'raf' });
  }
  closedRows() {
    return this.page.evaluate(() => window.twinApp.history().filter((r) => r.open === 0 && r.result));
  }
  click(text, group = '#actions') { return this.page.click(`${group} button:has-text("${text}")`); }

  // Arms a watcher inside the page (runs on every animation frame, so it reacts within one frame of
  // the page's own state refresh): slows to `slowTo`x once the grind reaches the purge/predictive
  // phases, and presses Pause the moment the controller reaches COMPLETED or TIMEOUT (the firmware
  // shows the result screen for 3 s of virtual time before the operator dismisses it).
  async armFreeze(slowTo = 5) {
    await this.page.evaluate((slow) => {
      window.__qaFrozen = null;
      const tick = () => {
        const s = window.twinApp.state();
        if (s) {
          if ((s.phase_name === 'COMPLETED' || s.phase_name === 'TIMEOUT') && !window.twinApp.paused) {
            document.getElementById('btn-pause').click();
            window.__qaFrozen = { phase: s.phase_name, t: s.t_s };
            return;
          }
          if (['PRIME_SETTLING', 'PURGE_CONFIRM', 'PREDICTIVE'].includes(s.phase_name) && window.twinApp.speed > slow) {
            const b = document.querySelector(`#speed-seg button[data-speed="${slow}"]`);
            if (b) b.click();
          }
        }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    }, slowTo);
  }
  // Change the page speed to `speed` the first animation frame the controller is in one of `phases`.
  async armSlow(phases, speed) {
    await this.page.evaluate(([ph, sp]) => {
      const tick = () => {
        const s = window.twinApp.state();
        if (s && ph.includes(s.phase_name)) {
          const b = document.querySelector(`#speed-seg button[data-speed="${sp}"]`);
          if (b) b.click();
          return;
        }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    }, [phases, speed]);
  }

  // Pause the page the first animation frame after `cond(s)` (an expression on the state object `s`) holds.
  async armPauseWhen(cond) {
    await this.page.evaluate((c) => {
      const f = new Function('s', 'return ' + c);
      window.__qaFrozen = null;
      const tick = () => {
        const s = window.twinApp.state();
        if (s && f(s) && !window.twinApp.paused) {
          document.getElementById('btn-pause').click();
          window.__qaFrozen = { phase: s.phase_name, t: s.t_s };
          return;
        }
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    }, cond);
  }
  async waitFrozen(timeout = 150000) {
    await this.page.waitForFunction(() => window.__qaFrozen !== null, null, { timeout, polling: 'raf' });
    await this.page.waitForTimeout(150); // let the paused page repaint
    return this.page.evaluate(() => window.__qaFrozen);
  }

  // Screenshots --------------------------------------------------------------------------
  async shot(what, selector) {
    const file = `${this.tag}_${what}.png`;
    const p = path.join(OUT, file);
    if (selector) {
      // the sticky header would cover the top of an element scrolled under it (capture artefact): hide it, without layout change
      await this.page.addStyleTag({ content: '.topbar{visibility:hidden !important}' }).then(async (h) => {
        await this.page.locator(selector).first().screenshot({ path: p });
        await h.evaluate((el) => el.remove());
      });
    }
    else {
      await this.page.evaluate(() => window.scrollTo(0, 0));
      await this.page.screenshot({ path: p, fullPage: true });
    }
    this.rep.shots.push(file);
    return p;
  }
  async shotBuf(selector) {
    const h = await this.page.addStyleTag({ content: '.topbar{visibility:hidden !important}' });
    try { return await this.page.locator(selector).first().screenshot(); } finally { await h.evaluate((el) => el.remove()); }
  }

  // Sampling the virtual screen bitmap (280x456 canvas pixels, independent of CSS scale).
  samplePanel(cx = 140, cy = 396, r = 45) {
    return this.page.evaluate(([cx, cy, r]) => {
      const c = document.getElementById('screen');
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      let lit = 0, inC = 0, green = 0, red = 0, white = 0;
      for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) {
        const i = (y * c.width + x) * 4, R = d[i], G = d[i + 1], B = d[i + 2];
        if (R || G || B) lit++;
        if ((x - cx) ** 2 + (y - cy) ** 2 <= r * r) {
          inC++;
          if (G > 110 && R < 90 && B < 90) green++;
          else if (R > 200 && G < 120 && B < 70) red++;
          else if (R > 200 && G > 200 && B > 200) white++;
        }
      }
      return { lit, inC, green, red, white, w: c.width, h: c.height };
    }, [cx, cy, r]);
  }

  // Fraction of pixels of a 2D canvas (by id) that differ from its dominant colour.
  canvasInk(id) {
    return this.page.evaluate((id) => {
      const c = document.getElementById(id);
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      const h = new Map();
      for (let i = 0; i < d.length; i += 4) { const k = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2]; h.set(k, (h.get(k) || 0) + 1); }
      let mode = 0; for (const v of h.values()) mode = Math.max(mode, v);
      return { w: c.width, h: c.height, ink: 1 - mode / (d.length / 4), colours: h.size };
    }, id);
  }

  // Screen coordinate of a point in panel pixels (for mouse taps).
  async panelToClient(x, y) {
    await this.page.locator('#screen').scrollIntoViewIfNeeded();
    const b = await this.page.locator('#screen').boundingBox();
    return { x: b.x + (x / PANEL_W) * b.width, y: b.y + (y / PANEL_H) * b.height };
  }
  async tapPanel(x, y, holdMs = 250) {
    const { x: cx, y: cy } = await this.panelToClient(x, y);
    await this.page.mouse.move(cx, cy);
    await this.page.mouse.down();
    await this.page.waitForTimeout(holdMs);
    await this.page.mouse.up();
  }

  // Active diagram node
  diagramActive() {
    return this.page.evaluate(() => {
      const g = document.querySelector('#diagram g.node.active');
      if (!g) return null;
      const r = g.querySelector('rect'); const cs = getComputedStyle(r);
      return { name: g.querySelector('text').textContent, fill: cs.fill, stroke: cs.stroke, cls: g.getAttribute('class') };
    });
  }
  historyText() { return this.page.evaluate(() => document.querySelector('#history tbody').innerText); }

  // Close the session: verifies the page stayed clean.
  async finish() {
    const ext = this.requests.filter((u) => !(u.startsWith('file:') || u.startsWith('data:') || u.startsWith('blob:')));
    this.rep.check(this.vp, this.scn, 'no console errors, page errors or failed requests', this.errors.length === 0,
      this.errors.slice(0, 3).join(' | '), 'blocker', this.repro);
    this.rep.check(this.vp, this.scn, 'no requests other than file:, data:, blob:', ext.length === 0, ext.slice(0, 3).join(' '), 'blocker', this.repro);
    if (this.warnings.length) this.rep.note(this.vp, this.scn, `${this.warnings.length} console warning(s): ${this.warnings.slice(0, 2).join(' | ').slice(0, 300)}`);
    await this.ctx.close();
  }
}

// ------------------------------------------------------------------------------------------
// PNG analysis in a scratch page (canvas decode): no image library needed.
export class PngTool {
  static async create(browser) {
    const t = new PngTool();
    t.ctx = await browser.newContext();
    t.page = await t.ctx.newPage();
    await t.page.setContent('<html><body></body></html>');
    await t.page.evaluate(() => {
      window.__load = async (b64) => {
        const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
        const c = document.createElement('canvas'); c.width = img.naturalWidth; c.height = img.naturalHeight;
        const x = c.getContext('2d', { willReadFrequently: true }); x.drawImage(img, 0, 0);
        return { img, data: x.getImageData(0, 0, c.width, c.height), w: c.width, h: c.height };
      };
      window.__stats = async (b64) => {
        const { data, w, h } = await window.__load(b64);
        const d = data.data; const hist = new Map();
        for (let i = 0; i < d.length; i += 4) { const k = ((d[i] >> 4) << 8) | ((d[i + 1] >> 4) << 4) | (d[i + 2] >> 4); hist.set(k, (hist.get(k) || 0) + 1); }
        let mode = 0; for (const v of hist.values()) mode = Math.max(mode, v);
        const n = d.length / 4; let distinct = 0; for (const v of hist.values()) if (v / n > 0.0002) distinct++;
        return { w, h, nonModeFrac: 1 - mode / n, distinct };
      };
      // fraction of pixels (inside rect given as fractions) whose max channel difference exceeds 20
      window.__diff = async (a, b, rect) => {
        const A = await window.__load(a), B = await window.__load(b);
        if (A.w !== B.w || A.h !== B.h) return { sizeMismatch: true, frac: 1, a: [A.w, A.h], b: [B.w, B.h] };
        const [x0, y0, x1, y1] = rect || [0, 0, 1, 1];
        const X0 = Math.floor(x0 * A.w), X1 = Math.ceil(x1 * A.w), Y0 = Math.floor(y0 * A.h), Y1 = Math.ceil(y1 * A.h);
        let n = 0, diff = 0;
        for (let y = Y0; y < Y1; y++) for (let x = X0; x < X1; x++) {
          const i = (y * A.w + x) * 4; n++;
          const m = Math.max(Math.abs(A.data.data[i] - B.data.data[i]), Math.abs(A.data.data[i + 1] - B.data.data[i + 1]), Math.abs(A.data.data[i + 2] - B.data.data[i + 2]));
          if (m > 20) diff++;
        }
        return { frac: diff / n, n, w: A.w, h: A.h };
      };
      // pixels of the light-tan colour of the grounds stream particles (idle scenes contain none)
      window.__orange = async (b64) => {
        const { data } = await window.__load(b64); const d = data.data; let n = 0;
        for (let i = 0; i < d.length; i += 4) if (d[i] >= 190 && d[i] <= 240 && d[i + 1] >= 140 && d[i + 1] <= 190 && d[i + 2] >= 90 && d[i + 2] <= 140 && d[i] - d[i + 1] >= 25 && d[i + 1] - d[i + 2] >= 25) n++;
        return n;
      };
      window.__scale = async (b64, width) => {
        const { img, w, h } = await window.__load(b64);
        const s = Math.min(1, width / w); const c = document.createElement('canvas');
        c.width = Math.round(w * s); c.height = Math.round(h * s);
        const x = c.getContext('2d'); x.imageSmoothingQuality = 'high'; x.drawImage(img, 0, 0, c.width, c.height);
        return c.toDataURL('image/png').split(',')[1];
      };
    });
    return t;
  }
  stats(buf) { return this.page.evaluate((b) => window.__stats(b), buf.toString('base64')); }
  diff(a, b, rect) { return this.page.evaluate(([x, y, r]) => window.__diff(x, y, r), [a.toString('base64'), b.toString('base64'), rect || null]); }
  orange(buf) { return this.page.evaluate((b) => window.__orange(b), buf.toString('base64')); }
  async scaled(buf, width) { return Buffer.from(await this.page.evaluate(([b, w]) => window.__scale(b, w), [buf.toString('base64'), width]), 'base64'); }
  close() { return this.ctx.close(); }
}

export function ensureDirs() {
  fs.mkdirSync(OUT, { recursive: true });
  fs.mkdirSync(SHOTS, { recursive: true });
  for (const f of fs.readdirSync(OUT)) if (/\.(png|csv|txt|json)$/.test(f)) fs.unlinkSync(path.join(OUT, f));
}
