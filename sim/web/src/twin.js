// Thin wrapper around one instance of the WASM module (the real firmware + plant).
// No controller logic lives here: this only forwards calls and parses snapshots.

export const PANEL_W = 280;
export const PANEL_H = 456;

export const ACTIONS = {
  PLACE_CUP: 1,
  REMOVE_CUP: 2,
  EMPTY_CUP: 3,
  LOAD_BEANS: 4,
  BUMP: 5,
  PRESS: 6,
  CLEAN_CHUTE: 7,
  WIPE_PLATFORM: 8,
};

export const FAULTS = {
  LC_DISCONNECT: 1,
  LC_STUCK: 2,
  LC_NOISE_BURST: 3,
  RELAY_STUCK_ON: 4,
  RELAY_STUCK_OFF: 5,
  MOTOR_STALL: 6,
  FEED_BLOCK: 7,
};

export class Twin {
  constructor(M) {
    this.M = M;
    const c = (name, ret, args) => M.cwrap('twin_' + name, ret, args);
    this.f = {
      init: c('init', 'number', ['number', 'number']),
      setScenario: c('set_scenario', 'number', ['string']),
      boot: c('boot', null, []),
      runMs: c('run_ms', null, ['number']),
      setParam: c('set_param', 'number', ['string', 'number']),
      getParam: c('get_param', 'number', ['string']),
      action: c('action', null, ['number', 'number']),
      fault: c('fault', null, ['number', 'number', 'number']),
      touch: c('touch', null, ['number', 'number', 'number']),
      framebuffer: c('framebuffer', 'number', []),
      framebufferDirty: c('framebuffer_dirty', 'number', []),
      operatorStart: c('operator_start', null, []),
      restartRequested: c('restart_requested', 'number', []),
      paramTable: c('param_table', 'string', []),
      stateJson: c('state_json', 'string', []),
      recordsJson: c('records_json', 'string', []),
      logRead: c('log_read', 'string', []),
      traceRead: c('trace_read', 'string', []),
      traceHeader: c('trace_header', 'string', []),
      persistSize: c('persist_size', 'number', []),
      persistExport: c('persist_export', 'number', ['number', 'number']),
      persistImport: c('persist_import', 'number', ['number', 'number']),
      traceSuppressHeader: c('trace_suppress_header', null, []),
      restartReason: c('restart_reason', 'number', []),
      operatorActive: c('operator_active', 'number', []),
    };
  }

  // Fresh world. Scenario first so the seeded preferences (target, purge mode) match.
  init(seed, target, scenarioJson) {
    if (scenarioJson) this.f.setScenario(scenarioJson);
    this.f.init(seed, target);
  }
  setScenario(json) { return this.f.setScenario(json); }
  boot() { this.f.boot(); }
  runMs(ms) { if (ms > 0) this.f.runMs(ms); }
  state() { return JSON.parse(this.f.stateJson()); }
  records() { return JSON.parse(this.f.recordsJson()); }
  paramTable() { return JSON.parse(this.f.paramTable()); }
  setParam(name, v) { return this.f.setParam(name, v); }
  getParam(name) { return this.f.getParam(name); }
  action(a, v = 0) { this.f.action(a, v); }
  fault(code, active, v = 0) { this.f.fault(code, active ? 1 : 0, v); }
  touch(x, y, pressed) { this.f.touch(Math.round(x), Math.round(y), pressed ? 1 : 0); }
  operatorStart() { this.f.operatorStart(); }
  restartReason() { return this.f.restartReason(); }
  operatorActive() { return this.f.operatorActive() !== 0; }
  restartRequested() { return this.f.restartRequested() !== 0; }
  traceHeader() { return this.f.traceHeader(); }
  traceSuppressHeader() { this.f.traceSuppressHeader(); }

  // Drain firmware log text (the native buffer is 64 KB; loop until empty).
  readLog() {
    let out = '';
    for (let i = 0; i < 16; i++) {
      const s = this.f.logRead();
      if (!s) break;
      out += s;
    }
    return out;
  }

  // Drain pending CSV trace rows (header first after init).
  readTrace() {
    let out = '';
    for (let i = 0; i < 8; i++) {
      const s = this.f.traceRead();
      if (!s) break;
      out += s;
      if (s.length < (1 << 20) - 2) break;
    }
    return out;
  }

  // Returns true when the framebuffer changed since the last call, and fills `rgba`
  // (Uint32Array view over ImageData, little-endian ABGR) when it did.
  takeFramebuffer(rgba) {
    if (!this.f.framebufferDirty()) return false;
    const ptr = this.f.framebuffer();
    // Re-create the view every time: memory growth detaches old buffers.
    const src = new Uint16Array(this.M.HEAPU8.buffer, ptr, PANEL_W * PANEL_H);
    for (let i = 0, n = src.length; i < n; i++) {
      const p = src[i];
      const r = (p >> 11) & 31;
      const g = (p >> 5) & 63;
      const b = p & 31;
      const r8 = (r << 3) | (r >> 2);
      const g8 = (g << 2) | (g >> 4);
      const b8 = (b << 3) | (b >> 2);
      rgba[i] = 0xff000000 | (b8 << 16) | (g8 << 8) | r8;
    }
    return true;
  }

  // Export persistent state (NVS, filesystem, plant, clock) as a detached byte copy.
  exportPersist() {
    const M = this.M;
    const size = Math.ceil(this.f.persistSize()) + 16;
    const ptr = M._malloc(size);
    const n = this.f.persistExport(ptr, size);
    const out = M.HEAPU8.slice(ptr, ptr + Math.max(0, n));
    M._free(ptr);
    return out;
  }

  importPersist(bytes) {
    const M = this.M;
    const ptr = M._malloc(bytes.length);
    M.HEAPU8.set(bytes, ptr);
    const rc = this.f.persistImport(ptr, bytes.length);
    M._free(ptr);
    return rc;
  }
}

// `createGrindTwin` is defined by the inlined emscripten script.
export async function createTwin() {
  // eslint-disable-next-line no-undef
  const M = await createGrindTwin();
  return new Twin(M);
}
