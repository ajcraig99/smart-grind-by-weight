// Visual QA suite for sim/dist/index.html (the grind-by-weight digital twin page).
//   node sim/qa/run_qa.mjs        (or: npm --prefix sim/qa test)
// Exit code 0 when no blocker/major defect was found. Writes sim/qa/REPORT.md, sim/qa/out/*, sim/qa/screenshots/*.
import fs from 'node:fs';
import path from 'node:path';
import { launch, Report, Session, PngTool, ensureDirs, OUT, SHOTS, PAGE_FILE } from './lib.mjs';
import { normal, runDry, cupRemoved, manual, faults, exports } from './scenarios.mjs';
import { narrow, speed, view3d, idleLayout } from './extra.mjs';
import { writeReport } from './report.mjs';

const t0 = Date.now();
const ALL_VIEWPORTS = [{ name: '1280x800', w: 1280, h: 800 }, { name: '1920x1080', w: 1920, h: 1080 }];
const only = process.argv.includes('--only') ? process.argv[process.argv.indexOf('--only') + 1].split(',') : null;
const vpOnly = process.argv.includes('--vp') ? process.argv[process.argv.indexOf('--vp') + 1] : null;
const VIEWPORTS = ALL_VIEWPORTS.filter((v) => !vpOnly || v.name === vpOnly);
ensureDirs();
const rep = new Report();
rep.pageBytes = fs.statSync(PAGE_FILE).size;
const browsers = [];
const plain = {};

function makeCtx(browser, vpDef) {
  return async (scn, repro, size) => {
    const vp = size ? `${size.w}x${size.h}` : vpDef.name;
    const S = new Session(browser, rep, vp, scn, repro);
    await S.open(size ? { width: size.w, height: size.h } : { width: vpDef.w, height: vpDef.h });
    return { rep, vp, S };
  };
}

async function guarded(vp, name, fn) {
  if (only && !only.some((o) => name.includes(o))) return;
  const started = Date.now();
  try {
    await fn();
  } catch (e) {
    rep.check(vp, name, 'scenario completed without an exception', false, String((e && e.message) || e).split('\n')[0].slice(0, 300), 'blocker', '');
  }
  console.log(`---- [${vp}] ${name} done in ${((Date.now() - started) / 1000).toFixed(1)} s`);
}

async function runViewport(vpDef) {
  const browser = await launch();
  browsers.push(browser);
  plain[vpDef.name] = browser;
  const ctxf = makeCtx(browser, vpDef);
  const vp = vpDef.name;
  await guarded(vp, 's0_layout', () => idleLayout(ctxf));
  await guarded(vp, 's1_normal', () => normal(ctxf));
  await guarded(vp, 's2_rundry', () => runDry(ctxf));
  await guarded(vp, 's3_cupremoved', () => cupRemoved(ctxf));
  await guarded(vp, 's5_manual', () => manual(ctxf));
  await guarded(vp, 's7_faults', () => faults(ctxf));
  await guarded(vp, 's8_exports', () => exports(ctxf));
  await guarded(vp, 's6_3d', () => view3d(ctxf));
  if (vpDef === VIEWPORTS[0]) await guarded('1024x768', 's0_narrow', () => narrow(ctxf));
}

// Functional scenarios run side by side (one browser per viewport); the speed test then runs alone
// so that it does not compete for CPU.
await Promise.all(VIEWPORTS.map((v, i) => runViewport(v)));
for (let i = 0; i < VIEWPORTS.length; i++) {
  await guarded(VIEWPORTS[i].name, 's4_speed', () => speed(makeCtx(plain[VIEWPORTS[i].name], VIEWPORTS[i])));
}

// Representative screenshots (scaled down) for committing.
if (!only) {
  try {
    const png = await PngTool.create(plain['1280x800']);
    const pick = [
      ['1280x800_s1_normal_completed_full.png', 760, '01-normal-18g-completed-dashboard.png'],
      ['1280x800_s1_normal_completed_screen.png', 0, '02-virtual-screen-completed.png'],
      ['1280x800_s2_rundry_timeout_full.png', 760, '03-run-dry-timeout-dashboard.png'],
      ['1280x800_s2_rundry_timeout_screen.png', 0, '04-virtual-screen-no-beans.png'],
      ['1280x800_s6_3d_grinding.png', 0, '05-3d-view-grinding.png'],
      ['1280x800_s6_3d_exploded.png', 0, '06-3d-view-exploded.png'],
    ];
    for (const f of fs.readdirSync(SHOTS)) fs.unlinkSync(path.join(SHOTS, f));
    for (const [src, width, dst] of pick) {
      const p = path.join(OUT, src);
      if (!fs.existsSync(p)) continue;
      let buf = fs.readFileSync(p);
      if (width) buf = await png.scaled(buf, width);
      fs.writeFileSync(path.join(SHOTS, dst), buf);
    }
    await png.close();
  } catch (e) { console.log('could not write screenshots/: ' + e.message); }
}
for (const b of browsers) await b.close();

rep.wallSeconds = (Date.now() - t0) / 1000;
const sorted = Object.fromEntries(Object.keys(rep.values).sort().map((k) => [k, rep.values[k]]));
fs.writeFileSync(path.join(OUT, 'values.json'), JSON.stringify(sorted, null, 2));
const exit = writeReport(rep, !!only);
console.log(`\n${rep.checks.filter((c) => c.ok).length}/${rep.checks.length} checks passed in ${rep.wallSeconds.toFixed(0)} s; exit ${exit}`);
process.exit(exit);
