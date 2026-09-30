# Morning summary (digital twin)

**Status:** all four tiers done and gated (native twin + Monte Carlo, browser dashboard, firmware UI on a
virtual screen, 3D twin + visual QA). See `sim/PROGRESS.md` for detail.

**What works**
- The unmodified firmware (setup/loop, all FreeRTOS tasks, grind controller, LVGL UI) runs on a
  deterministic virtual clock against a plant model. Bluetooth/Wi-Fi are stubbed as absent. Zero changes to `src/`.
- `grindsim` CLI: one grind (~0.3 s wall), batches, faults, resets. Same seed = byte-identical output;
  native and WASM traces are byte-identical too.
- Monte Carlo: 6300 runs, `sim/reports/montecarlo.md` with charts and every failure's reproduction command.
- Page `sim/dist/index.html` (3.1 MB, offline): dashboard, real firmware screen (clickable), 3D grinder,
  faults, 0.25x-20x. Visual QA: `sim/qa/REPORT.md` 178/178 checks.

**Rebuild** (Linux): `sim/tools/fetch_deps.sh && cmake -S sim/host -B sim/out/host -G Ninja && ninja -C sim/out/host && ctest --test-dir sim/out/host`
then `python3 sim/mc/run_mc.py` (6 min on 4 cores). Details: `sim/README.md`.

**Get the page:** open `sim/dist/index.html` on this branch on GitHub, "Download raw file", double-click.

**Top findings** (model-dependent magnitudes; `sim/FINDINGS.md`)
1. F4: one bump during grinding can end a grind 7 g short, reported as complete (MAX_PULSES).
2. F3: a stuck load-cell reading is not detected; motor runs up to 5 s on it; during pulses the cup overfills ~1.3 g.
3. F2: welded relay: session "completes", UI returns to ready, motor never stops, no warning.
4. F1: an 18 g single dose cannot reach 18.0 g on the first (purge) grind: ends "No beans?".
5. F11: controller aims at target - 0.03 g, so ~59 % of nominal results are just below the band.

**Broken / limits:** plant parameters are mostly placeholders (32/38), so magnitudes are model output;
both cores are serialised; Bluetooth/Wi-Fi not simulated; PlatformIO could not build here (registry blocked).
Page: dragging on the 3D grinder's screen swipes the firmware UI (by design, easy to trigger);
"Power-cycle firmware" reports reset reason SW; 20x speed measured only in headless software rendering.

**Next:** measure the real grinder (latency, coast, retention, load-cell noise) and replace placeholders;
decide on F2-F4, F11-F12 (firmware changes are out of scope here).
