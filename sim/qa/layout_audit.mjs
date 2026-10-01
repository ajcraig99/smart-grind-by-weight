// Visits every on-device screen in the twin, runs the firmware-side layout audit
// on each and fails if any screen has a defect. PNGs go to sim/qa/out/layout/.
//   node sim/qa/layout_audit.mjs            (needs sim/dist/index.html built)
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

const failing = new Set();
for (const [name, steps] of SCREENS) {
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
  if (stepsOk) await audit(name);
  await page.evaluate(() => window.twinApp.ui('ready', 2));
}

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
    await page.evaluate(() => window.twinApp.ui('ready', 2)).catch(() => {});
  }
}
await tuneFlow('tune-success', null, 'New Motor Latency:');
await tuneFlow('tune-failure', 'Relay stuck off', 'Using default:');

// The noise-check step follows a real calibration, which needs a weight on the scale.
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
    await page.evaluate(() => window.twinApp.ui('ready', 2)).catch(() => {});
    await act('Remove cup').catch(() => {});
  }
}
await calibrationNoiseFlow('calibration-noise');

await browser.close();
console.log(failing.size ? `${failing.size} screen(s) with layout defects` : 'all screens clean');
process.exit(failing.size ? 1 : 0);
