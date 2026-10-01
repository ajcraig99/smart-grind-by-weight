// Visits every on-device screen in the twin, runs the firmware-side layout audit
// on each and fails if any screen has a defect. PNGs go to sim/qa/out/layout/.
//   node sim/qa/layout_audit.mjs            (needs sim/dist/index.html built)
// A full run takes about 5+ minutes (the flows below run real firmware time, including
// a whole grind at 1x), so it is an on-demand check, not a per-commit gate.
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.join(here, '..', 'web', 'package.json'));
const { chromium } = require('playwright-core');
const outDir = path.join(here, 'out', 'layout');
fs.rmSync(outDir, { recursive: true, force: true });  // no stale PNGs from an earlier run
fs.mkdirSync(outDir, { recursive: true });
const chrome = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe';

const OK = '\uF00C';
// Icons must be bigger than this many matching pixels to count as drawn; a real warning
// icon is ~300 px, so a few stray anti-aliased pixels from other text do not count.
const WARNING_ICON_MIN_PIXELS = 20;
// [name, steps]; each step is [command, argument]. "wait" runs virtual time (ms).
const SCREENS = [
  ['ready-manual', [['ready', 0]]],
  ['ready-single', [['ready', 1]]],
  ['ready-double', [['ready', 2]]],
  ['ready-custom', [['ready', 3]]],
  ['ready-wifi', [['ready', 4]]],
  ['ready-menu', [['ready', 5]]],
  ['edit', [['ready', 2], ['state', 'EDIT']]],
  ['menu', [['menu']]],
  ['menu-scale', [['menu'], ['tap', 'Scale'], ['wait', 5000]]],  // the page tares on entry (up to 4 s)
  ['menu-firmware', [['menu'], ['tap', 'Firmware']]],
  ['menu-bluetooth', [['menu'], ['tap', 'Bluetooth']]],
  ['menu-wifi', [['menu'], ['tap', 'Wi-Fi']]],
  ['menu-display', [['menu'], ['tap', 'Display']]],
  ['menu-grind', [['menu'], ['tap', 'Grind Settings']]],
  ['menu-diagnostics', [['menu'], ['tap', 'Diagnostics']]],
  ['menu-info', [['menu'], ['tap', 'System Info']]],
  ['menu-logs', [['menu'], ['tap', 'Logs & Data']]],
  ['menu-stats', [['menu'], ['tap', 'Lifetime Stats']]],
  ['dialog-motor-test', [['menu'], ['tap', 'Motor Test']]],
  ['dialog-pulse-tune', [['menu'], ['tap', 'Pulse Tune']]],
  ['dialog-purge-logs', [['menu'], ['tap', 'Logs & Data'], ['tap', 'Purge Logs']]],
  ['dialog-factory-reset', [['menu'], ['tap', 'Logs & Data'], ['tap', 'Factory Reset']]],
  ['dialog-clear-warnings', [['menu'], ['tap', 'Diagnostics'], ['tap', 'Clear Warnings']]],
  ['dialog-remote-start', [['menu'], ['tap', 'Wi-Fi'], ['toggle', 'Remote']]],
  ['calibration-empty', [['state', 'CALIBRATION']]],
  ['calibration-weight', [['state', 'CALIBRATION'], ['tap', OK], ['wait', 3000]]],
  ['ota-failed', [['state', 'OTA_UPDATE_FAILED']]],
];

const browser = await chromium.launch({ executablePath: chrome, headless: true, args: ['--disable-gpu'] });
const page = await (await browser.newContext({ viewport: { width: 1400, height: 1000 } })).newPage();
// Surface page failures, so a boot that never finishes shows its cause.
page.on('pageerror', (err) => console.log(`page error: ${err.message}`));
page.on('console', (msg) => { if (msg.type() === 'error') console.log(`console error: ${msg.text()}`); });
await page.goto(pathToFileURL(path.join(here, '..', 'dist', 'index.html')).href);
await page.waitForFunction(() => window.twinApp && !window.twinApp.busy && window.twinApp.state()?.booted,
  null, { timeout: 90000 });

// Runs the firmware for `ms` of virtual time (the page keeps the world running).
async function waitVirtual(ms) {
  const t0 = await page.evaluate(() => window.twinApp.state().t_s);
  await page.waitForFunction((t) => window.twinApp.state().t_s >= t, t0 + ms / 1000, { timeout: 60000 });
}

async function savePng(file) {
  const png = await page.evaluate(() => document.getElementById('screen').toDataURL('image/png'));
  fs.writeFileSync(path.join(outDir, file), Buffer.from(png.split(',')[1], 'base64'));
}

// The audit sees only what is in view, so a scrolling menu page is audited at
// every scroll position. A defect already seen at an earlier position is reported
// once. `reachedBottom` is false when the page is too long to audit to its end.
const MAX_SCROLL_POSITIONS = 40;
const coordinateFree = (issue) => JSON.stringify(issue).replace(/ \[-?\d+,-?\d+\.\.-?\d+,-?\d+\]/g, '');

async function auditAllScrollPositions(name) {
  const issues = [];
  const seenEarlier = new Set();
  if (await page.evaluate(() => window.twinApp.ui('scroll', 'top'))) await page.waitForTimeout(400);
  for (let position = 1; position <= MAX_SCROLL_POSITIONS; position++) {
    const keysHere = [];
    for (const i of await page.evaluate(() => window.twinApp.layoutAudit())) {
      const key = coordinateFree(i);
      if (!seenEarlier.has(key)) issues.push(i);
      keysHere.push(key);
    }
    for (const key of keysHere) seenEarlier.add(key);
    await savePng(position === 1 ? `${name}.png` : `${name}-${position}.png`);
    if (!(await page.evaluate(() => window.twinApp.ui('scroll', '')))) return { issues, reachedBottom: true };
    await page.waitForTimeout(400);  // let the firmware loop draw the new position
  }
  return { issues, reachedBottom: false };
}

// One audited view: runs the layout audit at every scroll position, saves the
// PNG(s), prints PASS/FAIL and records a failure under `name`.
async function audit(name) {
  const { issues, reachedBottom } = await auditAllScrollPositions(name);
  if (issues.length) failing.add(name);
  console.log(`${issues.length ? 'FAIL' : 'PASS'}  ${name}`);
  for (const i of issues) console.log(`      ${i.rule}: ${i.a}${i.b ? '  vs  ' + i.b : ''}`);
  if (!reachedBottom) {
    console.log(`FAIL  ${name}: scroll limit reached; bottom not audited`);
    failing.add(name);
  }
}

// Return to the ready screen on a tab (errors ignored: this is cleanup).
const resetToReady = (tab = 2) => page.evaluate((t) => window.twinApp.ui('ready', t), tab).catch(() => {});

// Helpers for the flows below: click a twin-page button by its text.
async function clickButton(group, label) {
  await page.click(`${group} button:text-is("${label}")`);
  await page.waitForTimeout(400);
}
const act = (label) => clickButton('#actions', label);
const fault = (label) => clickButton('#faults', label);
const setSpeed = (x) => page.click(`#speed-seg button[data-speed="${x}"]`);

// Tap a label on the device screen; a missing one fails the flow.
async function uiStep(flow, cmd, arg = '') {
  const ok = await page.evaluate(([c, a]) => window.twinApp.ui(c, a), [cmd, String(arg)]);
  await page.waitForTimeout(400);
  if (!ok) throw new Error(`${flow}: step ${cmd} "${arg}" found nothing`);
}

// Waits (wall clock) until one of the labels is on the device screen.
async function waitForLabel(labels, timeoutMs = 120000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    for (const l of labels) {
      if (await page.evaluate((t) => window.twinApp.ui('find', t), l)) return l;
    }
    await page.waitForTimeout(500);
  }
  throw new Error(`none of ${JSON.stringify(labels)} appeared within ${timeoutMs / 1000} s`);
}

// ---- Reading the device screen: the warning icon and the ready-screen tab dots. ----
// Both read the canvas pixels. Panel coordinates are 280 x 456.
async function readCanvas(fn, arg) {
  return page.evaluate(async ([src, a]) => {
    const img = new Image();
    img.src = document.getElementById('screen').toDataURL('image/png');
    await img.decode();
    const c = document.createElement('canvas');
    c.width = img.width; c.height = img.height;
    const ctx = c.getContext('2d');
    ctx.drawImage(img, 0, 0);
    return new Function('ctx', 'img', 'a', src)(ctx, img, a);
  }, [fn, arg]);
}
// Warning-orange pixels (THEME_COLOR_WARNING 0xCC8800) in the icon strip: the
// warning icon sits left of the Bluetooth and Wi-Fi icons, top right.
const warningPixels = () => readCanvas(`
  const sx = img.width / 280, sy = img.height / 456;
  const d = ctx.getImageData(Math.floor(130 * sx), 0, Math.floor(110 * sx), Math.floor(50 * sy)).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] > 150 && d[i + 1] > 90 && d[i + 1] < 180 && d[i + 2] < 60) n++;
  return n;`);
// Left edge (panel px) of the wide white pill in the page-indicator dot row.
const tabPillLeft = () => readCanvas(`
  const sx = img.width / 280, sy = img.height / 456;
  const d = ctx.getImageData(0, Math.floor(330 * sy), img.width, 1).data;
  let run = 0;
  for (let x = 0; x < img.width; x++) {
    if (d[x * 4] > 200 && d[x * 4 + 1] > 200 && d[x * 4 + 2] > 200) { if (++run > 14 * sx) return (x - run + 1) / sx; }
    else run = 0;
  }
  return -1;`);

// Mouse input on the device screen, in panel coordinates. tapPanel/panelPoint intentionally
// duplicate the helpers in sim/qa/lib.mjs, because that module defaults to a Linux Chrome path.
async function panelPoint(x, y) {
  await page.locator('#screen').scrollIntoViewIfNeeded();
  const b = await page.locator('#screen').boundingBox();
  return { x: b.x + (x / 280) * b.width, y: b.y + (y / 456) * b.height };
}
async function tapPanel(x, y, holdMs = 250) {
  const p = await panelPoint(x, y);
  await page.mouse.move(p.x, p.y);
  await page.mouse.down();
  await page.waitForTimeout(holdMs);
  await page.mouse.up();
}
async function swipePanel(x0, x1, y) {
  const a = await panelPoint(x0, y);
  const b = await panelPoint(x1, y);
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  for (let i = 1; i <= 14; i++) { await page.mouse.move(a.x + ((b.x - a.x) * i) / 14, a.y); await page.waitForTimeout(100); }
  await page.mouse.up();
  await page.waitForTimeout(800);
}

// Taps the grind button (the same one that stops a grind) only while the motor runs.
async function stopGrindIfRunning() {
  const s = await page.evaluate(() => window.twinApp.state());
  if (s.motor_speed > 0.05) await tapPanel(140, 396);
}

const failing = new Set();
// Visits each [name, steps] screen. `after(name)` runs once the screen is audited.
async function visitScreens(list, namePrefix = '', after = null) {
  for (const [rawName, steps] of list) {
    const name = namePrefix + rawName;
    let stepsOk = true;
    for (const [cmd, arg = ''] of steps) {
      if (cmd === 'wait') { await waitVirtual(Number(arg)); continue; }
      const ok = await page.evaluate(([c, a]) => window.twinApp.ui(c, a), [cmd, String(arg)]);
      await page.waitForTimeout(400);  // let the firmware loop lay out and draw
      if (!ok) {
        console.log(`FAIL  ${name}: step ${cmd} "${arg}" found nothing`);
        failing.add(name);
        stepsOk = false;
        break;
      }
    }
    if (stepsOk) {
      await audit(name);
      if (after) await after(rawName, name);
    }
    await page.evaluate(() => window.twinApp.ui('ready', 2));
  }
}
await visitScreens(SCREENS);

// ---- Flows: screens that only exist after the firmware has done real work. ----
const RESULT_LABELS = ['New Motor Latency:', 'Using default:'];  // success, failure
async function tuneFlow(name, faultLabel, resultLabel) {
  let faultOn = false;
  try {
    await page.evaluate(() => window.twinApp.ui('ready', 2));
    await act('Place cup');
    await act('Load beans');
    await act('Load beans');
    if (faultLabel) { await fault(faultLabel); faultOn = true; }
    await uiStep(name, 'menu');
    await uiStep(name, 'tap', 'Pulse Tune');
    await uiStep(name, 'tap', 'START');
    if (!faultLabel) {
      await page.waitForTimeout(3000);  // real time at 1x: the console has a few lines, the tune is far from done
      await audit('tune-console');
    }
    await setSpeed(10);  // the rest of the tune is long; run it fast
    const got = await waitForLabel(RESULT_LABELS);
    if (got !== resultLabel) throw new Error(`expected "${resultLabel}", got "${got}"`);
    await page.waitForTimeout(400);
    await audit(name);
    await uiStep(name, 'tap', OK);
  } catch (err) {
    console.log(`FAIL  ${name}: ${err.message}`);
    failing.add(name);
  } finally {
    // Cleanup must always finish, so browser.close() is reached.
    if (faultOn) await fault(faultLabel).catch(() => {});  // toggles the fault off again
    await setSpeed(1).catch(() => {});
    await resetToReady();
  }
}
await tuneFlow('tune-success', null, 'New Motor Latency:');
await tuneFlow('tune-failure', 'Relay stuck off', 'Using default:');

// The noise-check step follows a real calibration, which needs a weight on the scale.
// Must run after the tune flows, at 1x speed (it waits in virtual time for the tare).
async function calibrationNoiseFlow(name) {
  try {
    await page.evaluate(() => window.twinApp.ui('ready', 2));
    await act('Remove cup');  // earlier flows leave a cup on the scale; tare must see it empty
    await uiStep(name, 'state', 'CALIBRATION');
    await uiStep(name, 'tap', OK);  // tare the empty scale
    await waitVirtual(3000);
    await page.fill('#actions input[aria-label="Place cup value"]', '100');  // a 100 g stand-in for the known weight
    await act('Place cup');
    await waitVirtual(2000);
    await uiStep(name, 'tap', OK);  // calibrate against the weight
    await waitForLabel(['NOISE CHECK'], 30000);
    await page.waitForTimeout(400);
    await audit(name);
  } catch (err) {
    console.log(`FAIL  ${name}: ${err.message}`);
    failing.add(name);
  } finally {
    await resetToReady();
    await act('Remove cup').catch(() => {});
    await page.fill('#actions input[aria-label="Place cup value"]', '').catch(() => {});  // later flows place the default cup
  }
}
await calibrationNoiseFlow('calibration-noise');

// ---- Grind flow: the prompts and result screens of one real grind, in a fixed order. ----
// Runs after the calibration flow and before the warning-icon flow. Nothing before it grinds
// (the tune flows run motor tests, the calibration flow only calibrates), so its first grind
// is the first one since boot and the grinder is still stale: the purge prompt is shown. The
// warning-icon flow grinds after it. The sequence:
//   purge prompt, grind (ring view), grind (chart view), complete, more grinds until the
//   hopper runs dry, out-of-beans prompt, status lines, "Cup moved?", error screen.
const uiStateName = async () => (await page.evaluate(() => window.twinApp.state())).ui_state_name;

// Waits (wall clock) until the firmware UI is in one of the named states; returns it.
async function waitForUiState(names, timeoutMs = 60000) {
  const deadline = Date.now() + timeoutMs;
  let last = '';
  while (Date.now() < deadline) {
    last = await uiStateName();
    if (names.includes(last)) return last;
    await page.waitForTimeout(200);
  }
  throw new Error(`none of ${JSON.stringify(names)} reached within ${timeoutMs / 1000} s (last state ${last})`);
}

// Starts grinds from an emptied cup until the hopper runs dry and the out-of-beans prompt
// shows. Earlier flows leave an unknown amount of beans in the hopper, so this loops.
async function reachRefillPrompt(maxGrinds = 8) {
  let started = 0;
  const deadline = Date.now() + 360000;
  while (Date.now() < deadline) {
    const state = await uiStateName();
    if (state === 'REFILL_CONFIRM') return;
    if (state === 'PURGE_CONFIRM') await tapPanel(200, 396);                                     // CONTINUE
    else if (state === 'GRIND_COMPLETE' || state === 'GRIND_TIMEOUT') await tapPanel(140, 396);  // OK / close
    else if (state === 'READY') {
      if (started >= maxGrinds) throw new Error(`no out-of-beans prompt after ${started} grinds`);
      started++;
      await act('Remove cup'); await act('Empty cup'); await act('Place cup');
      await page.waitForTimeout(1500);
      await tapPanel(140, 396);
    }
    await page.waitForTimeout(300);
  }
  throw new Error('out-of-beans prompt not reached within 6 minutes');
}

const releasePressShown = () => page.evaluate(
  () => [...document.querySelectorAll('#actions button')].some((b) => b.textContent === 'Release press'));

// Taps CONTINUE (the check button) until the prompt has gone, and says how many taps it took.
async function continuePrompt(promptState) {
  for (let tap = 1; tap <= 5; tap++) {
    await tapPanel(200, 396);
    await page.waitForTimeout(1200);
    if ((await uiStateName()) !== promptState) { if (tap > 1) console.log(`note: ${promptState} needed ${tap} taps`); return; }
  }
  throw new Error(`${promptState} did not continue after 5 taps`);
}

async function grindFlow() {
  const flow = 'grind-flow';
  let bumper = false;
  try {
    await resetToReady();
    await act('Place cup');
    await act('Load beans');
    await page.waitForTimeout(1500);
    await tapPanel(140, 396);  // start the first grind since boot

    // Purge prompt. No prompt here means an earlier flow ground: a script ordering bug.
    // The prompt follows the tare and the purge grind, so the state passes through GRINDING first.
    const first = await waitForUiState(['PURGE_CONFIRM', 'GRIND_COMPLETE', 'GRIND_TIMEOUT']);
    if (first === 'PURGE_CONFIRM') {
      await page.waitForTimeout(400);
      await audit('flow-purge');
      await continuePrompt('PURGE_CONFIRM');
    } else {
      console.log(`FAIL  flow-purge: no purge prompt (went to ${first} instead); the grinder was already purged since boot`);
      failing.add('flow-purge');
    }

    // The ring view mid-grind, then the chart view and back (a long press on the grind area).
    await waitForUiState(['GRINDING', 'GRIND_COMPLETE']);
    await page.waitForTimeout(1500);
    await audit('flow-grinding');
    await tapPanel(140, 200, 1500);
    await page.waitForTimeout(600);
    await audit('flow-chart');
    await tapPanel(140, 200, 1500);  // back to the ring view (saved when the screen returns to ready)
    await page.waitForTimeout(600);

    await waitForUiState(['GRIND_COMPLETE'], 120000);
    await page.waitForTimeout(400);
    await audit('flow-complete');
    await tapPanel(140, 396);
    await waitForUiState(['READY']);

    // Out of beans.
    await setSpeed(10);  // the extra grinds are long; run them fast
    await reachRefillPrompt();
    await setSpeed(1);
    await page.waitForTimeout(600);
    await audit('flow-refill');

    // The status lines: the check button while the scale is not settled. Bumps every 120 ms
    // keep it unsettled, so "Hold still..." shows, then (after the 5 s settle timeout)
    // "Scale not steady...".
    await page.evaluate(() => {
      window.__flowBumper = setInterval(() => [...document.querySelectorAll('#actions button')]
        .find((b) => b.textContent === 'Bump scale').click(), 120);
    });
    bumper = true;
    await tapPanel(200, 396, 100);
    await waitForLabel(['Hold still while the scale settles...'], 10000);
    await audit('flow-refill-status');
    await waitForLabel(['Scale not steady. Keep still, then press ' + OK + ' again.'], 15000);
    await page.evaluate(() => clearInterval(window.__flowBumper));
    bumper = false;
    await page.waitForTimeout(600);
    await audit('flow-refill-timeout');

    // "Cup moved?": a press on the scale moves the settled reading by more than 0.5 g.
    await act('Press on scale');
    await page.waitForTimeout(3000);
    await tapPanel(200, 396);
    await waitForLabel(['Cup moved?'], 10000);
    await page.waitForTimeout(400);
    await audit('flow-cup-moved');
    await uiStep(flow, 'tap', 'BACK');
    await act('Release press');
    await page.waitForTimeout(600);

    // Error screen: STOP at the out-of-beans prompt ends the grind as "No beans?".
    if ((await uiStateName()) !== 'REFILL_CONFIRM') throw new Error('not back at the out-of-beans prompt after BACK');
    await tapPanel(80, 396);
    await waitForUiState(['GRIND_TIMEOUT']);
    await page.waitForTimeout(600);
    await audit('flow-error');
    await tapPanel(140, 396);
    await waitForUiState(['READY']);
  } catch (err) {
    console.log(`FAIL  ${flow}: ${err.message}`);
    await savePng(`${flow}-failed.png`).catch(() => {});
    failing.add(flow);
  } finally {
    if (bumper) await page.evaluate(() => clearInterval(window.__flowBumper)).catch(() => {});
    await setSpeed(1).catch(() => {});
    if (await releasePressShown().catch(() => false)) await act('Release press').catch(() => {});
    await page.evaluate(() => window.twinApp.ui('tap', 'BACK')).catch(() => {});  // a dialog left open
    for (let i = 0; i < 3 && (await uiStateName().catch(() => 'READY')) !== 'READY'; i++) {
      const state = await uiStateName().catch(() => 'READY');
      await tapPanel(state === 'PURGE_CONFIRM' || state === 'REFILL_CONFIRM' ? 80 : 140, 396).catch(() => {});  // STOP / close
      await page.waitForTimeout(800);
    }
    await resetToReady();
    await act('Remove cup').catch(() => {});
  }
}
await grindFlow();

// The warning icon is drawn only on the ready screen. The mechanical-instability
// warning needs three weight drops of 0.4 g or more (200 ms apart) while the motor
// runs (GRIND_MECHANICAL_*). A 10 g "Bump scale" decays within a few control ticks, so
// bumps while the motor runs count as drops. The Manual tab grinds without a target, so
// the bumps cannot end the grind early (on a weight tab they push the reading past the target).
const WARNING_SCREENS = ['ready-double', 'edit', 'menu', 'calibration-empty', 'dialog-motor-test'];
async function raiseMechanicalWarning(name) {
  await page.fill('#actions input[aria-label="Bump scale value"]', '50');
  try {
    for (let attempt = 1; attempt <= 3; attempt++) {
      await page.evaluate(() => window.twinApp.ui('ready', 0));
      await page.waitForTimeout(600);
      await tapPanel(140, 396);  // the grind button
      let bumps = 0;
      const deadline = Date.now() + 90000;
      while (Date.now() < deadline && bumps < 12) {
        await page.waitForTimeout(200);
        const s = await page.evaluate(() => window.twinApp.state());
        if (s.phase_name === 'PURGE_CONFIRM') { await tapPanel(200, 396); continue; }  // CONTINUE
        if (s.ui_state_name === 'GRIND_COMPLETE' || s.phase_name === 'IDLE') break;
        if (s.motor_speed > 0.05) { await page.click(`#actions button:text-is("Bump scale")`); bumps++; }
      }
      await stopGrindIfRunning();
      await page.waitForTimeout(800);
      await page.evaluate(() => window.twinApp.ui('ready', 0));
      await page.waitForTimeout(800);
      if ((await warningPixels()) > WARNING_ICON_MIN_PIXELS) return;
      console.log(`${name}: attempt ${attempt} (${bumps} bumps) did not raise the warning`);
    }
    throw new Error('the warning icon never appeared on the ready screen');
  } finally {
    // A click or state() that threw mid-grind must not leave the motor running.
    await stopGrindIfRunning().catch(() => {});
    await page.fill('#actions input[aria-label="Bump scale value"]', '5').catch(() => {});  // the panel default
  }
}

async function warningIconFlow(name) {
  try {
    await page.evaluate(() => window.twinApp.ui('ready', 2));
    await act('Place cup');
    await act('Load beans');
    await act('Load beans');
    await page.waitForTimeout(2500);

    // With the icon container on screen a swipe still changes tab, and the grind button still starts a grind.
    const before = await tabPillLeft();
    await swipePanel(220, 60, 200);
    const after = await tabPillLeft();
    if (!(before >= 0 && after > before)) throw new Error(`swipe did not change tab (dot pill ${before} -> ${after})`);
    await swipePanel(60, 220, 200);
    if (Math.abs((await tabPillLeft()) - before) > 1) throw new Error('swipe back did not return to the first tab');

    await raiseMechanicalWarning(name);
    const shown = new Map();
    await visitScreens(SCREENS.filter(([n]) => WARNING_SCREENS.includes(n)), 'warning-', async (raw, full) => {
      const px = await warningPixels();
      shown.set(raw, px);
      const onReady = raw.startsWith('ready');
      const drawn = px > WARNING_ICON_MIN_PIXELS;
      if (onReady ? !drawn : drawn) {
        console.log(`FAIL  ${full}: warning icon ${onReady ? 'missing on' : 'drawn over'} this screen (${px} px)`);
        failing.add(full);
      }
    });
    console.log(`warning-icon pixels by screen: ${[...shown].map(([k, v]) => `${k}=${v}`).join(', ')}`);
  } catch (err) {
    console.log(`FAIL  ${name}: ${err.message}`);
    await savePng(`${name}-failed.png`).catch(() => {});
    failing.add(name);
  } finally {
    // Leave no warning, cup or open screen behind for the flows after this one.
    await resetToReady();
    await act('Remove cup').catch(() => {});
    await page.evaluate(() => window.twinApp.ui('menu', '')).catch(() => {});
    await page.waitForTimeout(400);
    for (const label of ['Diagnostics', 'Clear Warnings', 'CLEAR']) {
      await page.evaluate((t) => window.twinApp.ui('tap', t), label).catch(() => {});
      await page.waitForTimeout(400);
    }
    await resetToReady();
    await page.waitForTimeout(600);
    if ((await warningPixels().catch(() => 0)) > WARNING_ICON_MIN_PIXELS) {
      console.log(`FAIL  ${name}: warning icon still shown after Clear Warnings`);
      failing.add(name);
    }
  }
}
await warningIconFlow('warning-icon');

await browser.close();
console.log(failing.size ? `${failing.size} screen(s) with layout defects` : 'all screens clean');
process.exit(failing.size ? 1 : 0);
