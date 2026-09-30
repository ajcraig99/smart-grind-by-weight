// Headless acceptance test for sim/dist/index.html (run `npm run build` first).
//   PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm --prefix sim/web install
//   node sim/web/test/run.mjs [--chrome /path/to/chrome]
// Uses an already installed Chromium (never downloads one). Screenshots go to sim/web/test/out/.
import { chromium } from 'playwright-core';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const outDir = path.join(here, 'out');
const page_path = path.resolve(here, '..', '..', 'dist', 'index.html');
const argChrome = process.argv.indexOf('--chrome');
const chromePath = argChrome > 0 ? process.argv[argChrome + 1]
  : process.env.CHROME_PATH || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome';

fs.mkdirSync(outDir, { recursive: true });
for (const f of fs.readdirSync(outDir)) if (f.endsWith('.png') || f.endsWith('.csv') || f.endsWith('.txt')) fs.unlinkSync(path.join(outDir, f));

const results = [];
function check(name, ok, detail = '') {
  results.push({ name, ok });
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
}

const problems = [];
const browser = await chromium.launch({ executablePath: chromePath, headless: true, args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const context = await browser.newContext({ viewport: { width: 1280, height: 800 }, acceptDownloads: true });
const page = await context.newPage();
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') problems.push(`console.${m.type()}: ${m.text()}`); });
page.on('pageerror', (e) => problems.push('pageerror: ' + e.message));
page.on('requestfailed', (r) => problems.push('requestfailed: ' + r.url().slice(0, 100)));
const requests = [];
page.on('request', (r) => requests.push(r.url()));

const state = () => page.evaluate(() => window.twinApp.state());
const waitPhase = (name, timeout = 120000) =>
  page.waitForFunction((n) => window.twinApp.state() && window.twinApp.state().phase_name === n, name, { timeout, polling: 'raf' });
const waitPhaseNot = (name, timeout = 60000) =>
  page.waitForFunction((n) => window.twinApp.state() && window.twinApp.state().phase_name !== n, name, { timeout, polling: 'raf' });
const setSpeed = (sp) => page.click(`#speed-seg button[data-speed="${sp}"]`);
const ready = () => page.waitForFunction(() => window.twinApp && !window.twinApp.busy && window.twinApp.state() && window.twinApp.state().booted, null, { timeout: 60000 });
const closedRows = (world) => page.evaluate((w) => window.twinApp.history().filter((r) => r.world === w && r.open === 0 && r.result), world);
const waitClosed = (world, n, timeout = 180000) =>
  page.waitForFunction(([w, k]) => window.twinApp.history().filter((r) => r.world === w && r.open === 0 && r.result).length >= k, [world, n], { timeout, polling: 'raf' });
const shot = (name, el) => (el ? page.locator(el).screenshot({ path: path.join(outDir, name) }) : page.screenshot({ path: path.join(outDir, name), fullPage: true }));

const t0 = Date.now();
await page.goto(pathToFileURL(page_path).href);
await ready();
check('page loads from file:// and firmware boots', (await state()).booted === 1, `t=${(await state()).t_s}s`);
check('title', (await page.title()) === 'Grind-by-weight digital twin');
await page.waitForTimeout(500);
await shot('01-dashboard-ready.png');
await shot('01-screen-ready.png', '#screen');

// ---- layout sanity at 1280x800
const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
check('no horizontal page scroll at 1280x800', overflow <= 0, `overflow ${overflow}px`);
const canvasSize = await page.evaluate(() => { const c = document.getElementById('screen'); return [c.width, c.height]; });
check('virtual screen canvas is 280x456', canvasSize[0] === 280 && canvasSize[1] === 456);
const nonBlack = await page.evaluate(() => {
  const c = document.getElementById('screen');
  const d = c.getContext('2d').getImageData(0, 0, 280, 456).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] || d[i + 1] || d[i + 2]) n++;
  return n;
});
check('virtual screen shows firmware pixels', nonBlack > 2000, `${nonBlack} lit pixels`);

// ---- 3D twin renders
await page.waitForFunction(() => window.twinApp.scene3d && window.twinApp.scene3d.stats.frames > 2, null, { timeout: 30000 });
const probe0 = await page.evaluate(() => window.twinApp.scene3d.probe());
check('3D canvas exists and renders non-blank pixels', probe0.lit > 0.1 * probe0.total, `${probe0.lit}/${probe0.total} px differ from the background (${probe0.w}x${probe0.h})`);
await page.locator('#card-3d').screenshot({ path: path.join(outDir, '01-3d-idle.png') });

// ---- manual tap on the Play button
await setSpeed(5);
await page.click('#actions button:has-text("Load beans")');
await page.click('#actions button:has-text("Place cup")');
await page.waitForTimeout(400);
await page.locator('#screen').scrollIntoViewIfNeeded();
const box = await page.locator('#screen').boundingBox();
const tx = box.x + (140 / 280) * box.width;
const ty = box.y + (396 / 456) * box.height;
const before = (await state()).phase_name;
await page.mouse.move(tx, ty);
await page.mouse.down();
await page.waitForTimeout(250);
await page.mouse.up();
await waitPhaseNot('IDLE', 20000).catch(() => {});
const after = (await state()).phase_name;
check('manual tap on Play leaves IDLE', before === 'IDLE' && after !== 'IDLE', `${before} -> ${after}`);
await page.waitForTimeout(300);
await shot('02-screen-after-manual-tap.png', '#screen');

// ---- World 2: Auto grind with mixed speeds, screenshots
await page.fill('#in-seed', '1');
await setSpeed(20);
await page.check('#in-startpaused');
await page.click('#btn-boot');
await page.waitForFunction(() => window.twinApp.world === 2 && !window.twinApp.busy, null, { timeout: 60000 });
await ready();
check('restart world creates world 2', (await page.evaluate(() => window.twinApp.world)) === 2);
check('start paused holds the new world at power-on', await page.evaluate(() => window.twinApp.paused));
await setSpeed(2);
await page.click('#btn-auto');
await page.click('#btn-pause'); // resume
await waitPhase('PREDICTIVE', 120000);
await page.waitForFunction(() => window.twinApp.scene3d.particles > 15, null, { timeout: 30000, polling: 'raf' });
await page.click('#btn-pause');
await page.waitForTimeout(150);
await shot('03-dashboard-grinding.png');
await shot('03-screen-grinding.png', '#screen');
const probeG = await page.evaluate(() => ({ ...window.twinApp.scene3d.probe(), particles: window.twinApp.scene3d.particles, cmd: document.getElementById('led-cmd').className, contact: document.getElementById('led-contact').className }));
check('3D grounds stream visible while grinding', probeG.particles > 15 && probeG.lit > 0.1 * probeG.total, `${probeG.particles} particles in flight; LEDs: ${probeG.cmd} / ${probeG.contact}`);
await page.evaluate(() => window.scrollTo(0, 0));
await shot('03-3d-grinding.png', '#card-3d');
// exploded view
await page.click('#btn-explode');
await page.waitForFunction(() => window.twinApp.scene3d.explodeAmount >= 0.999, null, { timeout: 20000, polling: 'raf' });
await page.waitForTimeout(400);
const labels = await page.evaluate(() => window.twinApp.scene3d.labelsVisible);
check('exploded view toggles and shows labelled parts', labels >= 7, `${labels} labels visible`);
await page.evaluate(() => window.scrollTo(0, 0));
await shot('03-3d-exploded.png', '#card-3d');
await page.click('#btn-explode');
await page.waitForFunction(() => window.twinApp.scene3d.explodeAmount <= 0.001, null, { timeout: 20000, polling: 'raf' });
check('exploded view toggles back', (await page.evaluate(() => window.twinApp.scene3d.explodeAmount)) === 0);
await page.click('#btn-view-reset');
const grindState = await state();
check('grinding state captured', grindState.phase_name === 'PREDICTIVE' && grindState.motor_speed > 0.1, `motor ${grindState.motor_speed}`);
await page.click('#btn-step'); // single step while paused
const afterStep = await state();
check('step advances virtual time by 20 ms', Math.abs(afterStep.t_s - grindState.t_s - 0.02) < 0.0015, `${grindState.t_s} -> ${afterStep.t_s}`);
await page.click('#btn-pause');
await setSpeed(5);
await waitPhase('COMPLETED', 120000);
await setSpeed(0.5);
await page.click('#btn-pause');
await page.waitForTimeout(150);
await shot('04-dashboard-completed.png');
await shot('04-screen-completed.png', '#screen');
await page.click('#btn-pause');
await setSpeed(20);
await waitClosed(2, 1);
const runA = (await closedRows(2))[0];
check('history shows a COMPLETED grind', runA && runA.terminal === 'COMPLETED' && runA.result === 'SUCCESS',
  runA ? `fw ${runA.fw_final_g} g, true ${runA.true_cup_g} g, pulses ${runA.pulses}` : 'no row');
await page.waitForTimeout(400);
await shot('05-dashboard-history.png');
await page.click('#view-seg button[data-view="grind"]');
await page.waitForTimeout(300);
await shot('06-dashboard-whole-grind.png');
await page.click('#view-seg button[data-view="window"]');

// ---- Determinism: same seed, fresh world, 20x the whole way
await page.fill('#in-seed', '1');
await page.click('#btn-boot');
await page.waitForFunction(() => window.twinApp.world === 3 && !window.twinApp.busy, null, { timeout: 60000 });
await ready();
await setSpeed(20);
await page.click('#btn-auto');
await page.click('#btn-pause'); // resume
await page.waitForTimeout(1500);
console.log('INFO  speed readout at 20x:', await page.textContent('#ratio'));
await waitClosed(3, 1);
const runB = (await closedRows(3))[0];
const keys = ['t_start_s', 't_end_s', 'terminal', 'result', 'error', 'target_g', 'fw_final_g', 'true_cup_end_g', 'true_cup_g', 'pulses', 'motor_on_s'];
const same = keys.every((k) => runA[k] === runB[k]);
check('determinism: same seed gives the same final record (run A mixed speeds, run B 20x)', same,
  same ? `fw ${runB.fw_final_g} g, true ${runB.true_cup_g} g, pulses ${runB.pulses}, motor ${runB.motor_on_s} s`
    : `A=${JSON.stringify(runA)} B=${JSON.stringify(runB)}`);
await page.fill('#in-seed', '2');
await page.uncheck('#in-startpaused');
await page.click('#btn-boot');
await page.waitForFunction(() => window.twinApp.world === 4 && !window.twinApp.busy, null, { timeout: 60000 });
await ready();
await page.click('#btn-auto');
await waitClosed(4, 1);
const runC = (await closedRows(4))[0];
check('different seed gives a different record (sanity)', keys.some((k) => runA[k] !== runC[k]),
  `seed2: fw ${runC.fw_final_g} g, true ${runC.true_cup_g} g, pulses ${runC.pulses}`);

// ---- faults, power cycle, exports
await page.click('#faults button:has-text("Load cell stuck")');
check('fault toggle shows active', (await page.getAttribute('#faults button:has-text("Load cell stuck")', 'aria-pressed')) === 'true');
await page.click('#faults button:has-text("Load cell stuck")');
const tBefore = (await state()).t_s;
await page.click('#btn-powercycle');
await page.waitForFunction(() => !window.twinApp.busy, null, { timeout: 30000 });
await ready();
await page.waitForTimeout(800);
const tAfter = (await state()).t_s;
check('power cycle keeps the physical world and reboots the firmware', (await state()).booted === 1 && tAfter >= tBefore,
  `t ${tBefore} -> ${tAfter}, phase ${(await state()).phase_name}`);
const hist = await page.evaluate(() => window.twinApp.history().filter((r) => r.result).length);
check('history survives power cycle and world restarts', hist >= 3, `${hist} closed rows`);

for (const [sel, name, minBytes] of [['#btn-trace-csv', 'trace.csv', 5000], ['#btn-history-csv', 'history.csv', 100], ['#btn-log-txt', 'log.txt', 500]]) {
  const [dl] = await Promise.all([page.waitForEvent('download'), page.click(sel)]);
  const p = path.join(outDir, name);
  await dl.saveAs(p);
  const size = fs.statSync(p).size;
  const head = fs.readFileSync(p, 'utf8').split('\n')[0];
  check(`download ${name} (${dl.suggestedFilename()})`, size >= minBytes, `${size} bytes, first line: ${head.slice(0, 60)}`);
}

// ---- param slider round trip
await page.fill('#param-filter', 'flow_nominal_gps');
const slider = page.locator('.param[data-name="flow_nominal_gps"] input[type=range]');
await slider.focus();
await slider.press('End');
const val = await page.locator('.param[data-name="flow_nominal_gps"] input[type=number]').inputValue();
check('parameter slider moves and updates the value', parseFloat(val) > 2.9, `flow_nominal_gps = ${val}`);
await page.click('.param[data-name="flow_nominal_gps"] button');
const val2 = await page.locator('.param[data-name="flow_nominal_gps"] input[type=number]').inputValue();
check('parameter reset returns to default', parseFloat(val2) === 1.9, `flow_nominal_gps = ${val2}`);

// ---- 3D collapse, raycast touch on the 3D screen, WebGL-less fallback
await page.click('#btn-3d-toggle');
const f0 = await page.evaluate(() => window.twinApp.scene3d.stats.frames);
await page.waitForTimeout(600);
const f1 = await page.evaluate(() => window.twinApp.scene3d.stats.frames);
check('collapsed 3D card stops rendering', f1 === f0 && (await page.isHidden('#three-wrap')), `frames ${f0} -> ${f1}`);
await page.click('#btn-3d-toggle');
await page.waitForFunction((f) => window.twinApp.scene3d.stats.frames > f, f1, { timeout: 10000 });
check('3D card resumes rendering when expanded', true);

await page.fill('#in-seed', '3');
await setSpeed(5);
await page.click('#btn-boot');
await page.waitForFunction(() => window.twinApp.world === 5 && !window.twinApp.busy, null, { timeout: 60000 });
await ready();
await page.click('#actions button:has-text("Load beans")');
await page.click('#actions button:has-text("Place cup")');
await page.locator('#three-wrap').scrollIntoViewIfNeeded();
await page.waitForTimeout(800);
const [cx, cy] = await page.evaluate(() => window.twinApp.scene3d.panelToClient(140, 396));
const beforeTouch = (await state()).phase_name;
await page.mouse.move(cx, cy);
await page.mouse.down();
await page.waitForTimeout(250);
await page.mouse.up();
await waitPhaseNot('IDLE', 20000).catch(() => {});
const afterTouch = (await state()).phase_name;
check('touch on the 3D screen (raycast) starts a grind', beforeTouch === 'IDLE' && afterTouch !== 'IDLE', `${beforeTouch} -> ${afterTouch}`);

{
  const b2 = await chromium.launch({ executablePath: chromePath, headless: true, args: ['--no-sandbox', '--disable-gpu', '--disable-3d-apis', '--disable-webgl', '--disable-webgl2'] });
  const p2 = await (await b2.newContext({ viewport: { width: 1280, height: 800 } })).newPage();
  const errs2 = [];
  p2.on('pageerror', (e) => errs2.push(e.message));
  await p2.goto(pathToFileURL(page_path).href);
  await p2.waitForFunction(() => window.twinApp && !window.twinApp.busy && window.twinApp.state() && window.twinApp.state().booted, null, { timeout: 60000 });
  const msg = await p2.locator('#three-wrap .three-msg').count();
  const still = (await p2.evaluate(() => window.twinApp.state().booted)) === 1;
  check('without WebGL: clear message, rest of the page still works', msg === 1 && still && errs2.length === 0, errs2.join('|'));
  await p2.screenshot({ path: path.join(outDir, '07-no-webgl.png') });
  await b2.close();
}

// ---- network and errors
const external = requests.filter((u) => !u.startsWith('file://') && !u.startsWith('data:') && !u.startsWith('blob:'));
check('no network requests (only file://, data:, blob:)', external.length === 0, external.slice(0, 3).join(' '));
check('no console errors, warnings or page errors', problems.length === 0, problems.slice(0, 3).join(' | '));

await browser.close();
const failed = results.filter((r) => !r.ok);
console.log(`\n${results.length - failed.length}/${results.length} checks passed in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
process.exit(failed.length ? 1 : 0);
