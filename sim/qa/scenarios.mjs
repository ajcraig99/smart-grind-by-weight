// QA scenarios for sim/dist/index.html. Each takes a fresh page and (mostly) a fresh fixed-seed world.
import fs from 'node:fs';
import path from 'node:path';
import { OUT } from './lib.mjs';
import { layoutCheck } from './extra.mjs';

const SEED = 1;
const FAULT_LABELS = [
  ['lc_disconnect', 'Load cell disconnect'], ['lc_stuck', 'Load cell stuck'], ['lc_noise', 'Noise burst'],
  ['relay_on', 'Relay stuck on'], ['relay_off', 'Relay stuck off'], ['motor_stall', 'Motor stall'], ['feed_block', 'Bean feed blocked'],
];

const rowValues = (r) => ({
  terminal: r.terminal, result: r.result, error: r.error, target_g: r.target_g, fw_final_g: r.fw_final_g,
  true_cup_g: r.true_cup_g, pulses: r.pulses, motor_on_s: r.motor_on_s, t_start_s: r.t_start_s, t_end_s: r.t_end_s,
});

// Run a scripted Auto grind to its terminal phase, frozen on the result screen. Returns the frozen info.
async function autoGrindAndFreeze(S, speed = 20) {
  await S.setSpeed(speed);
  await S.armFreeze(5);
  await S.page.click('#btn-auto');
  await S.pauseToggle(); // resume from the start-paused hold
  return S.waitFrozen();
}

// ------------------------------------------------------------------------------------------ 1
export async function normal(ctxf) {
  const { rep, vp, S } = await ctxf('s1_normal', 'Set seed 1, target 18, beans 22, start paused; Boot world; speed 20x; Auto grind; wait for COMPLETED.');
  await S.freshWorld({ seed: SEED, target: 18, beans: 22 });
  const s0 = await S.state();
  rep.check(vp, S.scn, 'fresh world is paused at power-on (t = 6.5 s)', s0.t_s === 6.5 && s0.phase_name === 'IDLE', `t=${s0.t_s} phase=${s0.phase_name}`, 'minor', S.repro);
  const before = await S.canvasInk('plot-weight');
  const cols0 = await S.page.evaluate(() => ['card-screen', 'card-3d', 'card-traces'].map((id) => document.getElementById(id) ? Math.round(document.getElementById(id).getBoundingClientRect().width) : -1));
  const fr = await autoGrindAndFreeze(S, 20);
  const s = await S.state();
  rep.check(vp, S.scn, 'controller reaches COMPLETED', fr.phase === 'COMPLETED', `frozen in ${fr.phase} at ${fr.t.toFixed(2)} s`, 'blocker', S.repro);
  // result screen: green OK button
  const px = await S.samplePanel(140, 396, 45);
  const greenFrac = px.green / px.inC;
  rep.check(vp, S.scn, 'virtual screen shows the completion screen (green OK button at 140,396)', greenFrac > 0.4, `green ${(greenFrac * 100).toFixed(0)}% of the 45 px circle, ${px.lit} lit pixels`, 'major', S.repro);
  rep.check(vp, S.scn, 'canvas is 280x456 and not black', px.w === 280 && px.h === 456 && px.lit > 3000, `${px.w}x${px.h}, ${px.lit} lit`, 'major', S.repro);
  // traces drawn
  const inkW = await S.canvasInk('plot-weight'), inkF = await S.canvasInk('plot-flow'), inkE = await S.canvasInk('plot-error'), inkS = await S.canvasInk('plot-strip');
  rep.check(vp, S.scn, 'weight trace drawn (pixels changed vs before the grind)', inkW.ink > before.ink + 0.003, `ink ${(before.ink * 100).toFixed(2)}% -> ${(inkW.ink * 100).toFixed(2)}%`, 'major', S.repro);
  rep.check(vp, S.scn, 'flow, error and relay/motor strips drawn', inkF.ink > 0.004 && inkE.ink > 0.004 && inkS.ink > 0.004,
    `ink flow ${(inkF.ink * 100).toFixed(2)}%, error ${(inkE.ink * 100).toFixed(2)}%, strip ${(inkS.ink * 100).toFixed(2)}%`, 'major', S.repro);
  const rows = await S.page.evaluate(() => window.twinApp.traceRows());
  rep.check(vp, S.scn, 'trace rows recorded', rows > 2000, `${rows} rows (10 ms each)`, 'major', S.repro);
  // state diagram
  const act = await S.diagramActive();
  rep.check(vp, S.scn, 'state diagram highlights COMPLETED', act && act.name === 'COMPLETED', JSON.stringify(act), 'major', S.repro);
  if (act) rep.value(vp, S.scn, 'diagram_active_colour', `${act.fill} / ${act.stroke}`);
  rep.check(vp, S.scn, 'page state reports COMPLETED / firmware result SUCCESS', s.phase_name === 'COMPLETED' && s.fw_result === 1, `phase ${s.phase_name}, fw_result ${s.fw_result}, motor ${s.motor_speed}`, 'major', S.repro);
  const cols1 = await S.page.evaluate(() => ['card-screen', 'card-3d', 'card-traces'].map((id) => document.getElementById(id) ? Math.round(document.getElementById(id).getBoundingClientRect().width) : -1));
  const shift = Math.max(...cols0.map((w, i) => Math.abs(w - cols1[i])));
  rep.check(vp, S.scn, 'panel widths stay stable between idle and the result screen (no layout shift)', shift <= 2,
    `widths of screen/3D/traces cards idle ${cols0.join('/')} px -> after grind ${cols1.join('/')} px (max shift ${shift} px)`, 'minor', S.repro);
  await S.shot('completed_full');
  await S.shot('completed_screen', '#screen');
  await S.shot('completed_diagram', '#card-diagram');
  await S.shot('completed_traces', '#card-traces');

  // finish at 20x, wait for the closed history row
  await S.setSpeed(20);
  await S.pauseToggle();
  await S.waitClosed(1);
  const r = (await S.closedRows())[0];
  const err = Math.abs(r.fw_final_g - 18.0);
  rep.check(vp, S.scn, 'history row: COMPLETED / SUCCESS', r.terminal === 'COMPLETED' && r.result === 'SUCCESS', `${r.terminal} / ${r.result}`, 'blocker', S.repro);
  rep.check(vp, S.scn, 'firmware final weight within 0.05 g of 18.0 g', err <= 0.05, `final ${r.fw_final_g} g (error ${err.toFixed(3)} g), true cup ${r.true_cup_g} g, pulses ${r.pulses}, motor ${r.motor_on_s} s`, 'major', S.repro);
  rep.check(vp, S.scn, 'true cup mass within 0.1 g of 18.0 g', Math.abs(r.true_cup_g - 18) <= 0.1, `true ${r.true_cup_g} g`, 'minor', S.repro);
  rep.check(vp, S.scn, 'history table row rendered in the DOM', (await S.historyText()).includes('SUCCESS'), '', 'major', S.repro);
  const fin = await S.state();
  rep.check(vp, S.scn, 'mass conserved (|error| < 1e-6 g) and no safety stop', Math.abs(fin.conservation_error_g) < 1e-6 && !fin.fw_safety_stop, `conservation ${fin.conservation_error_g}`, 'major', S.repro);
  rep.value(vp, S.scn, 'row', rowValues(r));
  await S.page.waitForTimeout(300);
  await S.shot('history_full');
  await S.page.click('#view-seg button[data-view="grind"]');
  await S.page.waitForTimeout(300);
  await S.shot('whole_grind_traces', '#card-traces');
  await S.finish();
}

// ------------------------------------------------------------------------------------------ 2
export async function runDry(ctxf) {
  const { rep, vp, S } = await ctxf('s2_rundry', 'Set seed 1, target 18, beans 18.0, purge action discard, start paused; Boot world; Auto grind at 20x.');
  await S.freshWorld({ seed: SEED, target: 18, beans: 18, purge: 'discard' });
  const fr = await autoGrindAndFreeze(S, 20);
  rep.check(vp, S.scn, 'controller ends in TIMEOUT', fr.phase === 'TIMEOUT', `frozen in ${fr.phase} at ${fr.t.toFixed(2)} s`, 'major', S.repro);
  const px = await S.samplePanel(140, 396, 45);
  rep.check(vp, S.scn, 'virtual screen shows the error screen (non-black pixels)', px.lit > 2000, `${px.lit} lit pixels`, 'major', S.repro);
  const act = await S.diagramActive();
  rep.check(vp, S.scn, 'state diagram highlights TIMEOUT', act && act.name === 'TIMEOUT', JSON.stringify(act), 'major', S.repro);
  if (act) {
    const m = /rgb\((\d+), (\d+), (\d+)/.exec(act.fill) || [];
    const [R, G, B] = [m[1], m[2], m[3]].map(Number);
    const red = R > G * 1.4 && R > B * 1.4;
    rep.check(vp, S.scn, 'highlighted TIMEOUT node is drawn red', red, `fill ${act.fill}, stroke ${act.stroke}`, 'minor', S.repro);
    rep.value(vp, S.scn, 'diagram_active_colour', `${act.fill} / ${act.stroke}`);
  }
  await S.shot('timeout_full');
  await S.shot('timeout_screen', '#screen');
  await S.shot('timeout_diagram', '#card-diagram');
  await S.setSpeed(20);
  await S.pauseToggle();
  await S.waitClosed(1);
  const r = (await S.closedRows())[0];
  rep.check(vp, S.scn, 'history row: TIMEOUT with "No beans?"', r.terminal === 'TIMEOUT' && /no beans/i.test(r.error || ''), `${r.terminal} / ${r.result} / "${r.error}", fw ${r.fw_final_g} g, true cup ${r.true_cup_g} g`, 'major', S.repro);
  const ht = await S.historyText();
  rep.check(vp, S.scn, 'error text visible in the history table', /no beans/i.test(ht), ht.replace(/\s+/g, ' ').slice(0, 120), 'major', S.repro);
  const tw = await S.page.evaluate(() => { const w = document.querySelector('#card-history .table-wrap'); return [w.scrollWidth, w.clientWidth]; });
  rep.check(vp, S.scn, 'history table fits its card (all columns visible without sideways scrolling)', tw[0] <= tw[1] + 1, `table width ${tw[0]} px in a ${tw[1]} px wide container`, 'cosmetic', S.repro);
  rep.value(vp, S.scn, 'row', rowValues(r));
  await S.page.waitForTimeout(300);
  await S.shot('history_full');
  await S.finish();
}

// ------------------------------------------------------------------------------------------ 3
export async function cupRemoved(ctxf) {
  const { rep, vp, S } = await ctxf('s3_cupremoved', 'Seed 1, target 18, beans 22, start paused; Boot; speed 5x; Auto grind; 2 s after PREDICTIVE starts click Remove cup.');
  await S.freshWorld({ seed: SEED, target: 18, beans: 22 });
  await S.setSpeed(5);
  await S.page.click('#btn-auto');
  await S.pauseToggle();
  await S.waitPhase('PREDICTIVE', 120000);
  const t0 = (await S.state()).t_s;
  await S.waitVirtual(t0 + 2.0);
  const pre = await S.state();
  await S.armFreeze(5); // armed before the click: the error screen is only shown for 3 s of virtual time
  await S.click('Remove cup');
  const fr = await S.waitFrozen(60000);
  rep.check(vp, S.scn, 'cup removed in PREDICTIVE (precondition)', pre.phase_name === 'PREDICTIVE' || pre.phase_name.startsWith('PULSE'), `phase ${pre.phase_name} at ${pre.t_s} s`, 'minor', S.repro);
  rep.check(vp, S.scn, 'controller ends in TIMEOUT', fr.phase === 'TIMEOUT', `frozen in ${fr.phase}`, 'major', S.repro);
  const act = await S.diagramActive();
  rep.check(vp, S.scn, 'state diagram highlights TIMEOUT', act && act.name === 'TIMEOUT', JSON.stringify(act), 'major', S.repro);
  const px = await S.samplePanel(140, 396, 45);
  rep.check(vp, S.scn, 'virtual screen shows the error screen (non-black pixels)', px.lit > 2000, `${px.lit} lit pixels`, 'major', S.repro);
  await S.shot('timeout_full');
  await S.shot('timeout_screen', '#screen');
  await S.setSpeed(20);
  await S.pauseToggle();
  await S.waitClosed(1);
  const r = (await S.closedRows())[0];
  rep.check(vp, S.scn, 'history error text is "Err: neg wt"', /neg wt/i.test(r.error || '') && r.terminal === 'TIMEOUT', `${r.terminal} / ${r.result} / "${r.error}"`, 'major', S.repro);
  const ht = await S.historyText();
  rep.check(vp, S.scn, 'error text visible in the history table', /neg wt/i.test(ht), ht.replace(/\s+/g, ' ').slice(0, 120), 'major', S.repro);
  rep.value(vp, S.scn, 'row', { terminal: r.terminal, result: r.result, error: r.error }); // timing of the click varies with wall time
  await S.shot('history_full');
  await S.finish();
}

// ------------------------------------------------------------------------------------------ 5
export async function manual(ctxf) {
  const { rep, vp, S } = await ctxf('s5_manual', 'Boot world; click Load beans and Place cup; click the Play button on the virtual screen (centre x=140,y=396); click it again while grinding.');
  await S.freshWorld({ seed: SEED, target: 18, beans: 22, startPaused: false });
  await S.setSpeed(1);
  await S.click('Load beans');
  await S.click('Place cup');
  await S.page.waitForTimeout(2500); // ~2.5 s virtual at 1x: reading settles before START
  const idle = await S.samplePanel(140, 396, 45);
  rep.check(vp, S.scn, 'idle screen shows the red Play button', idle.red / idle.inC > 0.4, `red ${(100 * idle.red / idle.inC).toFixed(0)}% of circle`, 'minor', S.repro);
  await S.shot('idle_screen', '#screen');
  const p0 = (await S.state()).phase_name;
  await S.tapPanel(140, 396, 250);
  await S.waitPhaseNot('IDLE', 20000).catch(() => {});
  const p1 = await S.state();
  rep.check(vp, S.scn, 'tap on Play starts the grind (phase leaves IDLE)', p0 === 'IDLE' && p1.phase_name !== 'IDLE', `${p0} -> ${p1.phase_name}, ui ${p1.ui_state_name}`, 'blocker', S.repro);
  await S.setSpeed(5);
  await S.armSlow(['PREDICTIVE'], 1);
  await S.waitPhase(['PURGE_CONFIRM', 'PREDICTIVE'], 60000).catch(() => {});
  const pz = await S.state();
  if (pz.phase_name === 'PURGE_CONFIRM') {
    await S.page.waitForTimeout(300);
    await S.shot('purge_prompt_screen', '#screen');
    await S.tapPanel(200, 396, 250); // CONTINUE (right button of the purge prompt)
  }
  await S.waitPhase(['PREDICTIVE', 'PULSE_SETTLING', 'PULSE_DECISION', 'PULSE_EXECUTE'], 30000).catch(() => {});
  await S.page.waitForTimeout(1200);
  const p2 = await S.state();
  await S.shot('grinding_screen', '#screen');
  await S.shot('grinding_full');
  const grindPx = await S.samplePanel(140, 396, 45);
  rep.check(vp, S.scn, 'purge prompt answered with CONTINUE, grind proceeds into the main (predictive/pulse) phases', pz.phase_name === 'PURGE_CONFIRM' && ['PREDICTIVE', 'PULSE_SETTLING', 'PULSE_DECISION', 'PULSE_EXECUTE'].includes(p2.phase_name),
    `purge prompt seen: ${pz.phase_name}; now ${p2.phase_name}, motor ${p2.motor_speed.toFixed(2)}, weight ${p2.fw_weight_g} g`, 'major', S.repro);
  rep.note(vp, S.scn, `while grinding (phase ${p2.phase_name}) the button circle is red ${(100 * grindPx.red / grindPx.inC).toFixed(0)}%, white ${(100 * grindPx.white / grindPx.inC).toFixed(0)}%`);
  // second tap: Stop
  await S.tapPanel(140, 396, 250);
  let p3 = null;
  try { await S.waitPhase('IDLE', 20000); p3 = await S.state(); } catch { p3 = await S.state(); }
  rep.check(vp, S.scn, 'tap on Stop (same button) ends the grind (phase IDLE)', p3.phase_name === 'IDLE', `tapped in ${p2.phase_name}; now ${p3.phase_name}, ui ${p3.ui_state_name}, motor ${p3.motor_speed}`, 'major', S.repro);
  await S.page.waitForTimeout(400);
  const after = await S.state();
  rep.check(vp, S.scn, 'motor is off after Stop', after.motor_speed < 0.01 && after.relay_pin === 0, `motor ${after.motor_speed}, relay pin ${after.relay_pin}`, 'major', S.repro);
  await S.shot('after_stop_screen', '#screen');
  rep.value(vp, S.scn, 'flow', { started: p1.phase_name !== 'IDLE', stopped_to_idle: p3.phase_name === 'IDLE' });
  await S.finish();
}

// ------------------------------------------------------------------------------------------ 7
export async function faults(ctxf) {
  const { rep, vp, S } = await ctxf('s7_faults', 'Boot world; while idle click each fault button twice (on, off).');
  await S.freshWorld({ seed: SEED, target: 18, beans: 22, startPaused: false });
  await S.setSpeed(1);
  const effects = [];
  for (const [id, label] of FAULT_LABELS) {
    const sel = `#faults button[data-fault="${id}"]`;
    const exists = await S.page.locator(sel).count();
    if (!exists) { rep.check(vp, S.scn, `fault button "${label}" exists`, false, 'missing', 'major', S.repro); continue; }
    await S.page.click(sel);
    const on = (await S.page.getAttribute(sel, 'aria-pressed')) === 'true';
    const cls = await S.page.evaluate((s) => document.querySelector(s).classList.contains('active-fault'), sel);
    await S.page.waitForTimeout(700);
    const mid = await S.state();
    effects.push(`${id}: hx_connected=${mid.hx_connected}, relay_contact=${mid.relay_contact}, motor=${mid.motor_speed.toFixed(2)}, fw_scale_fault=${mid.fw_scale_fault}`);
    await S.page.click(sel);
    const off = (await S.page.getAttribute(sel, 'aria-pressed')) === 'false';
    await S.page.waitForTimeout(300);
    rep.check(vp, S.scn, `${label}: button shows active, then inactive`, on && cls && off, `on=${on} class=${cls} off=${off}`, 'major', S.repro);
    if (id === 'lc_disconnect') rep.check(vp, S.scn, 'Load cell disconnect reaches the plant (hx_connected 0 while active)', mid.hx_connected === 0, `hx_connected ${mid.hx_connected}`, 'minor', S.repro);
    if (id === 'relay_on') rep.check(vp, S.scn, 'Relay stuck on reaches the plant (contact closed while active)', mid.relay_contact === 1, `relay_contact ${mid.relay_contact}`, 'minor', S.repro);
  }
  const end = await S.state();
  rep.check(vp, S.scn, 'all faults cleared: load cell connected, motor off, relay open', end.hx_connected === 1 && end.relay_contact === 0 && end.motor_speed < 0.01, `hx ${end.hx_connected}, contact ${end.relay_contact}, motor ${end.motor_speed}`, 'major', S.repro);
  rep.note(vp, S.scn, 'state while each fault was active (idle): ' + effects.join('; '));
  // active state must stay visible while the pointer is still over the button (it is right after the click)
  const fsel = '#faults button[data-fault="motor_stall"]';
  const restBg = await S.page.evaluate((s) => getComputedStyle(document.querySelector(s)).backgroundColor, fsel);
  await S.page.click(fsel);
  await S.page.waitForTimeout(300);
  const hovBg = await S.page.evaluate((s) => getComputedStyle(document.querySelector(s)).backgroundColor, fsel);
  const rgb = (c) => (/rgb\((\d+), (\d+), (\d+)/.exec(c) || []).slice(1).map(Number);
  const [hr, hg] = rgb(hovBg);
  rep.check(vp, S.scn, 'an active fault button is drawn red while the pointer is still over it', hr > hg * 1.5,
    `background at rest ${restBg}; active and hovered ${hovBg} (the .active-fault rule is overridden by button:hover)`, 'minor', S.repro);
  await S.shot('fault_active_hovered', '#card-faults');
  await S.page.mouse.move(5, 5);
  await S.page.waitForTimeout(200);
  await S.shot('fault_active', '#card-faults');
  await S.click('Motor stall', '#faults');
  await S.shot('full');
  await S.finish();
}

// ------------------------------------------------------------------------------------------ 8
export async function exports(ctxf) {
  const { rep, vp, S } = await ctxf('s8_exports', 'Boot world, Auto grind at 20x until the history row closes, then click Trace CSV, History CSV and Log text.');
  await S.freshWorld({ seed: SEED, target: 18, beans: 22 });
  await S.setSpeed(20);
  await S.page.click('#btn-auto');
  await S.pauseToggle();
  await S.waitClosed(1);
  await S.page.waitForTimeout(600);
  for (const [sel, name, test] of [
    ['#btn-trace-csv', 'trace.csv', (t, lines) => {
      const h = lines[0].split(',');
      return { ok: h[0] === 't_ms' && h.includes('fw_weight_g') && h.includes('scale_true_g') && lines.length > 3000 && lines[1].split(',').length === h.length,
        detail: `${lines.length - 1} rows, ${h.length} columns, first: ${lines[0].slice(0, 70)}` };
    }],
    ['#btn-history-csv', 'history.csv', (t, lines) => ({
      ok: lines[0].startsWith('world,index,t_start_s,t_end_s,terminal,result,error,target_g,fw_final_g') && lines.length >= 2 && /COMPLETED/.test(lines[1]),
      detail: `${lines.length - 1} data row(s), header: ${lines[0].slice(0, 70)}; row: ${(lines[1] || '').slice(0, 90)}` })],
    ['#btn-log-txt', 'log.txt', (t, lines) => ({
      ok: lines.length > 20 && /WeightSensor|HX711|Grind/i.test(t),
      detail: `${lines.length} lines, first: ${lines[0].slice(0, 60)}` })],
  ]) {
    const [dl] = await Promise.all([S.page.waitForEvent('download', { timeout: 15000 }), S.page.click(sel)]);
    const p = path.join(OUT, `${S.tag}_${name}`);
    await dl.saveAs(p);
    const text = fs.readFileSync(p, 'utf8');
    const lines = text.split('\n').filter((l, i, a) => l !== '' || i < a.length - 1);
    const res = test(text, lines);
    rep.check(vp, S.scn, `${name} download is non-empty with the expected header (${dl.suggestedFilename()})`, text.length > 100 && res.ok,
      `${text.length} bytes; ${res.detail}`, 'major', S.repro);
    rep.value(vp, S.scn, name + '_header', lines[0].slice(0, 120));
  }
  await layoutCheck(S, 'after a grind (history and log populated)');
  await S.shot('full');
  await S.finish();
}
