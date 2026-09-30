// Layout, speed and 3D-view checks.
import { PngTool } from './lib.mjs';

// ------------------------------------------------------------------------------------------ layout
export async function layoutCheck(S, label, { narrow = false } = {}) {
  const { rep, vp } = S;
  const r = await S.page.evaluate(() => {
    const de = document.documentElement;
    const vis = (e) => e.getClientRects().length > 0 && getComputedStyle(e).visibility !== 'hidden';
    const boxes = (els) => els.filter(vis).map((e) => ({ id: e.id || e.className.toString().slice(0, 30), r: e.getBoundingClientRect() }));
    const overlaps = (list) => {
      const out = [];
      for (let i = 0; i < list.length; i++) for (let j = i + 1; j < list.length; j++) {
        const a = list[i].r, b = list[j].r;
        const ix = Math.min(a.right, b.right) - Math.max(a.left, b.left);
        const iy = Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top);
        if (ix > 2 && iy > 2) out.push(`${list[i].id} x ${list[j].id} (${ix.toFixed(0)}x${iy.toFixed(0)} px)`);
      }
      return out;
    };
    const cards = boxes([...document.querySelectorAll('.card')]);
    const top = boxes([...document.querySelectorAll('.topbar .group, .topbar .brand')]);
    const controls = boxes([...document.querySelectorAll('.topbar .group > *')]);
    const vw = de.clientWidth;
    const clipped = [...document.querySelectorAll('button, label, h1, h2, dt, dd, th, .badge, .sub')]
      .filter((e) => vis(e) && e.scrollWidth > e.clientWidth + 2 && ['hidden', 'clip'].includes(getComputedStyle(e).overflowX))
      .map((e) => (e.textContent || '').trim().slice(0, 30));
    return {
      hscroll: de.scrollWidth - de.clientWidth,
      cardOverlaps: overlaps(cards),
      topOverlaps: overlaps(top).concat(overlaps(controls)),
      offRight: cards.filter((c) => c.r.right > vw + 1 || c.r.left < -1).map((c) => c.id),
      clipped,
      cardCount: cards.length,
      vw,
    };
  });
  rep.check(vp, S.scn, `${label}: no horizontal page scroll`, r.hscroll <= 0, `scrollWidth - clientWidth = ${r.hscroll} px at ${r.vw} px`, 'major', S.repro);
  rep.check(vp, S.scn, `${label}: no overlapping panels (${r.cardCount} cards)`, r.cardOverlaps.length === 0, r.cardOverlaps.slice(0, 3).join('; '), 'major', S.repro);
  rep.check(vp, S.scn, `${label}: header controls do not overlap`, r.topOverlaps.length === 0, r.topOverlaps.slice(0, 3).join('; '), 'minor', S.repro);
  rep.check(vp, S.scn, `${label}: no panel extends past the viewport edge`, r.offRight.length === 0, r.offRight.join(', '), 'major', S.repro);
  rep.check(vp, S.scn, `${label}: no clipped text in buttons/labels`, r.clipped.length === 0, r.clipped.slice(0, 5).join(' | '), 'cosmetic', S.repro);
  return r;
}

// ------------------------------------------------------------------------------------------ narrow
export async function narrow(ctxf) {
  const { rep, vp, S } = await ctxf('s0_narrow', 'Open the page in a 1024x768 window (no scaling); check scrolling and overlaps.', { w: 1024, h: 768 });
  await layoutCheck(S, '1024 wide', { narrow: true });
  await S.shot('full');
  // the 3D view only renders while on screen: scroll to it (single-column layout puts it below the fold)
  if ((await S.page.locator('#card-3d canvas').count()) > 0) {
    await S.page.locator('#card-3d').scrollIntoViewIfNeeded();
    await S.page.waitForTimeout(1500);
    const png = await PngTool.create(S.browser);
    const st = await png.stats(await S.shotBuf('#card-3d canvas'));
    await png.close();
    rep.check(vp, S.scn, '3D canvas renders after scrolling into view at 1024 px', st.nonModeFrac > 0.01 && st.distinct > 6, `${(st.nonModeFrac * 100).toFixed(1)}% off the dominant colour, ${st.distinct} colours`, 'major', S.repro);
    await S.shot('3d_card', '#card-3d');
  }
  await S.finish();
}

// ------------------------------------------------------------------------------------------ speed
export async function speed(ctxf) {
  const { rep, vp, S } = await ctxf('s4_speed', 'Boot world, choose 20x, click Auto grind (repeat grinds for 10 s wall) and compare virtual time with wall time.');
  await S.freshWorld({ seed: 1, target: 18, beans: 22, startPaused: false });
  await S.setSpeed(20);
  const driver = (async () => {
    const t0 = Date.now();
    while (Date.now() - t0 < 10500) {
      const dis = await S.page.evaluate(() => document.getElementById('btn-auto').disabled);
      if (!dis) {
        await S.click('Remove cup'); await S.click('Empty cup'); await S.click('Place cup');
        await S.page.click('#btn-auto');
      }
      await S.page.waitForTimeout(150);
    }
  })();
  const meas = S.page.evaluate(() => new Promise((res) => {
    let last = null, tF = null, wF = null, tL = 0, wL = 0; const t0 = performance.now(); const readouts = [];
    let actV = 0, actW = 0, prevPhase = null, prevT = 0, prevW = 0;
    const tick = () => {
      const now = performance.now(); const s = window.twinApp.state();
      if (s && s.t_s !== last) {
        if (prevPhase !== null && prevPhase !== 'IDLE') { actV += s.t_s - prevT; actW += (now - prevW) / 1000; }
        prevPhase = s.phase_name; prevT = s.t_s; prevW = now;
        last = s.t_s; if (tF === null) { tF = s.t_s; wF = now; } tL = s.t_s; wL = now;
      }
      const m = /×([\d.]+) achieved/.exec(document.getElementById('ratio').textContent);
      if (m) { const v = parseFloat(m[1]); if (!readouts.length || readouts[readouts.length - 1].v !== v) readouts.push({ v }); }
      if (now - t0 < 10000) requestAnimationFrame(tick); else res({ tF, wF, tL, wL, actV, actW, readouts: readouts.map((x) => x.v) });
    };
    requestAnimationFrame(tick);
  }));
  const [m] = await Promise.all([meas, driver]);
  const ratio = (m.tL - m.tF) / ((m.wL - m.wF) / 1000);
  const hist = await S.page.evaluate(() => window.twinApp.history().filter((r) => r.result).length);
  const ro = m.readouts.slice().sort((a, b) => a - b);
  rep.numbers[`speed_${vp}`] = { activeRatio: m.actV / m.actW, activeVirtual: m.actV, activeWall: m.actW, ratio, virtual_s: m.tL - m.tF, wall_s: (m.wL - m.wF) / 1000, readoutMin: ro[0], readoutMedian: ro[Math.floor(ro.length / 2)], grinds: hist };
  const sev = ratio >= 10 ? 'minor' : ratio >= 5 ? 'minor' : 'major';
  rep.check(vp, S.scn, 'achieved virtual/wall ratio >= 10x at the 20x setting', ratio >= 10,
    `${ratio.toFixed(1)}x over ${((m.wL - m.wF) / 1000).toFixed(1)} s wall (${(m.tL - m.tF).toFixed(0)} s virtual, ${hist} grinds closed); while a grind was active (phase not IDLE) ${(m.actV / m.actW).toFixed(1)}x over ${m.actW.toFixed(1)} s wall; page read-out min ${ro[0]}, median ${ro[Math.floor(ro.length / 2)]}`, sev, S.repro);
  await S.shot('full');
  await S.finish();
}

async function pngSize(png, buf) { const st = await png.stats(buf); return `${st.w}x${st.h}`; }

// ------------------------------------------------------------------------------------------ 3D
export async function view3d(ctxf) {
  const { rep, vp, S } = await ctxf('s6_3d', 'Boot world (running at 1x); in the 3D twin card drag to orbit, click Exploded view, click Hide 3D / Show 3D; load beans + cup and tap Play on the 3D screen.');
  const png = await PngTool.create(S.browser);
  try {
    const CANVAS = '#card-3d canvas';
    const present = (await S.page.locator('#card-3d').count()) > 0;
    if (!present) {
      rep.note(vp, S.scn, '3D card (#card-3d) not present in this build of sim/dist/index.html: 3D checks skipped');
      rep.numbers[`view3d_${vp}`] = 'absent';
      await S.finish();
      return;
    }
    const hasCanvas = (await S.page.locator(CANVAS).count()) > 0;
    rep.check(vp, S.scn, '3D card contains a WebGL canvas (or a clear "unavailable" message)', hasCanvas, hasCanvas ? '' : (await S.page.locator('#three-wrap').innerText()).slice(0, 120), 'major', S.repro);
    if (!hasCanvas) { await S.finish(); return; }
    await S.freshWorld({ seed: 1, target: 18, beans: 22, startPaused: false });
    await S.setSpeed(1);
    await S.page.locator('#card-3d').scrollIntoViewIfNeeded();
    await S.page.waitForTimeout(1500);
    const base = await S.shotBuf(CANVAS);
    const st = await png.stats(base);
    const probe = await S.page.evaluate(() => (window.twinApp.scene3d ? window.twinApp.scene3d.probe() : null));
    rep.check(vp, S.scn, '3D canvas is not blank', st.nonModeFrac > 0.01 && st.distinct > 6, `${(st.nonModeFrac * 100).toFixed(1)}% pixels off the dominant colour, ${st.distinct} colours, ${st.w}x${st.h}${probe ? `; page probe: ${probe.lit}/${probe.total} lit` : ''}`, 'major', S.repro);
    await S.shot('idle', CANVAS);
    await S.shot('idle_card', '#card-3d');
    await S.shot('idle_full');

    // orbit drag
    const box = await S.page.locator(CANVAS).boundingBox();
    // Start on empty background: a drag that starts on the grinder's screen is forwarded to the firmware as a swipe.
    const cx = box.x + box.width * 0.10, cy = box.y + box.height * 0.45;
    await S.page.mouse.move(cx, cy);
    await S.page.mouse.down();
    await S.page.mouse.move(cx + box.width * 0.30, cy - box.height * 0.05, { steps: 12 });
    await S.page.mouse.up();
    await S.page.waitForTimeout(900);
    const orbit = await S.shotBuf(CANVAS);
    const d1 = await png.diff(base, orbit);
    rep.check(vp, S.scn, 'orbit drag changes the 3D image', d1.frac > 0.02, `${(d1.frac * 100).toFixed(1)}% of pixels changed`, 'major', S.repro);
    await S.shot('orbited', CANVAS);
    await S.page.click('#btn-view-reset');
    await S.page.waitForTimeout(1500);
    const reset = await S.shotBuf(CANVAS);
    const dr = await png.diff(base, reset);
    rep.check(vp, S.scn, 'Reset view returns to the default camera', dr.frac < 0.03, `${(dr.frac * 100).toFixed(1)}% differ from the initial view`, 'minor', S.repro);

    // exploded view toggle
    const before = reset;
    await S.page.click('#btn-explode');
    await S.page.waitForTimeout(2200);
    const pressed = (await S.page.getAttribute('#btn-explode', 'aria-pressed')) === 'true';
    const after = await S.shotBuf(CANVAS);
    const d2 = await png.diff(before, after);
    const labels = await S.page.evaluate(() => (window.twinApp.scene3d ? window.twinApp.scene3d.labelsVisible : -1));
    rep.check(vp, S.scn, 'exploded view: button pressed, image changes, part labels shown', pressed && d2.frac > 0.02 && labels !== 0, `aria-pressed=${pressed}, ${(d2.frac * 100).toFixed(1)}% pixels changed, ${labels} labels visible`, 'major', S.repro);
    await S.shot('exploded', CANVAS);
    await S.shot('exploded_card', '#card-3d');
    await S.page.click('#btn-explode');
    await S.page.waitForTimeout(2200);
    const back = await S.shotBuf(CANVAS);
    const d3 = await png.diff(before, back);
    rep.check(vp, S.scn, 'toggling exploded view off restores the assembled image', d3.frac < 0.03, `${(d3.frac * 100).toFixed(1)}% differ from before`, 'minor', S.repro);

    // hide / show
    await S.page.click('#btn-3d-toggle');
    await S.page.waitForTimeout(300);
    const hidden = !(await S.page.locator(CANVAS).isVisible());
    await S.page.click('#btn-3d-toggle');
    await S.page.waitForTimeout(800);
    const shown = await S.page.locator(CANVAS).isVisible();
    rep.check(vp, S.scn, 'Hide 3D collapses the canvas and Show 3D restores it', hidden && shown, `hidden=${hidden}, shown=${shown}`, 'minor', S.repro);

    // particle stream: idle (beans + cup loaded) versus grinding; start by tapping the screen shown on the 3D grinder
    await S.click('Load beans');
    await S.click('Place cup');
    await S.page.waitForTimeout(2500);
    const idle = await S.shotBuf(CANVAS);
    await S.shot('idle_loaded', CANVAS);
    const pt = await S.page.evaluate(() => (window.twinApp.scene3d ? window.twinApp.scene3d.panelToClient(140, 396) : null));
    let viaRaycast = false;
    if (pt) {
      await S.page.mouse.move(pt[0], pt[1]);
      await S.page.mouse.down();
      await S.page.waitForTimeout(250);
      await S.page.mouse.up();
      try { await S.waitPhaseNot('IDLE', 8000); viaRaycast = true; } catch { /* fall back to the 2D screen */ }
    }
    rep.check(vp, S.scn, 'tapping the Play button on the 3D grinder screen (raycast) starts the grind', viaRaycast, pt ? `tap at page ${pt[0].toFixed(0)},${pt[1].toFixed(0)}` : 'scene3d.panelToClient not exposed', 'major', S.repro);
    if (!viaRaycast) await S.tapPanel(140, 396, 250);
    await S.setSpeed(5);
    await S.waitPhase(['PURGE_CONFIRM', 'PREDICTIVE'], 60000).catch(() => {});
    if ((await S.state()).phase_name === 'PURGE_CONFIRM') {
      await S.page.waitForTimeout(400);
      await S.tapPanel(200, 396, 250); // CONTINUE (keeps the purged grounds as dose)
    }
    await S.armPauseWhen("s.phase_name === 'PREDICTIVE' && s.flow_cup_gps > 0.8");
    await S.waitFrozen(60000).catch(() => {});
    await S.page.waitForTimeout(1200); // paused: let the 3D view render the frozen frame
    const s = await S.state();
    const grinding = await S.shotBuf(CANVAS);
    const parts = await S.page.evaluate(() => (window.twinApp.scene3d ? window.twinApp.scene3d.particles : -1));
    await S.shot('grinding', CANVAS);
    await S.shot('grinding_card', '#card-3d');
    await S.shot('grinding_full');
    // Stream region: central band below the grinder body (found by viewing grinding.png); also report the whole canvas.
    const orangeIdle = await png.orange(idle), orangeGrind = await png.orange(grinding);
    const szI = await pngSize(png, idle), szG = await pngSize(png, grinding);
    const same = szI === szG;
    const d4 = same ? await png.diff(idle, grinding, [0.30, 0.45, 0.70, 0.85]) : null;
    rep.check(vp, S.scn, 'particle stream visible while grinding (tan grounds pixels appear; stream band differs when the canvas size is unchanged)',
      s.flow_burr_gps > 0.1 && orangeGrind - orangeIdle >= 20,
      `phase ${s.phase_name}, flow at burrs ${s.flow_burr_gps} g/s, at cup ${s.flow_cup_gps} g/s; tan particle pixels idle ${orangeIdle} -> grinding ${orangeGrind}; ${parts} particle slots in use; ${d4 ? (d4.frac * 100).toFixed(2) + '% of the stream band changed' : 'canvas size differs between the two shots (' + szI + ' vs ' + szG + '), band diff not comparable'}`, 'major', S.repro);
    rep.value(vp, S.scn, 'stream_seen', orangeGrind - orangeIdle >= 20);
    await S.finish();
  } finally {
    await png.close();
  }
}

// ------------------------------------------------------------------------------------------ idle layout
export async function idleLayout(ctxf) {
  const { S } = await ctxf('s0_layout', 'Open the page at this window size and inspect the initial layout.');
  await layoutCheck(S, 'initial layout');
  await S.shot('full');
  await S.finish();
}
