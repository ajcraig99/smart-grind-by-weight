# Digital twin of the grind-by-weight firmware

This directory holds two things:

1. **The digital twin** (everything below): the real, unmodified firmware from `src/` running on a
   virtual clock against a physics model of the grinder, the grounds path and the load cell. It builds
   natively (Monte Carlo harness `grindsim`) and to WebAssembly (the offline page `sim/dist/index.html`).
2. The older **Windows desktop UI simulator** (`sim/main.cpp`, `sim/CMakeLists.txt`, `sim/platform/`,
   `sim/*.ps1`, three `*_test.cpp`); unchanged, see the section at the end.

## Use the page (no build needed)

Open `sim/dist/index.html` from this branch on GitHub, choose "Download raw file", and double-click the
downloaded file. It runs offline in Chrome, Edge or Firefox (one file: the WebAssembly firmware, the
plant model and the dashboard are all inside it).

## Build and run natively (Linux)

```bash
sim/tools/fetch_deps.sh                         # LVGL 9.5.0 into sim/.deps (once)
sudo apt-get install -y g++-multilib ninja-build   # 32-bit host build (ILP32 like the ESP32)
cmake -S sim/host -B sim/out/host -G Ninja && ninja -C sim/out/host
ctest --test-dir sim/out/host                   # plant, scheduler, smoke and determinism tests

# one grind: trace CSV, firmware log, summary row, PNG snapshots of the firmware screen
sim/out/host/grindsim --seed 7 --scenario sim/scenarios/normal.json \
    --out run.csv --log log.txt --summary summary.csv --screens shots/
# batch: one fresh process per seed, summary rows in seed order
sim/out/host/grindsim --batch 200 --seed 1000 --jobs 4 --scenario sim/scenarios/normal.json --summary s.csv
# plant parameters: --params file.json or --set name=value (names in sim/plant/PARAMS.md)
```

Monte Carlo report: `python3 sim/mc/run_mc.py` (see `sim/mc/README.md`) writes
`sim/reports/montecarlo.md`.

## Build the page

```bash
source /path/to/emsdk/emsdk_env.sh     # any recent emsdk; tested with emcc 6.0.10
sim/wasm/build.sh                      # -> sim/out/wasm/grindtwin.js (WASM embedded)
npm --prefix sim/web install && npm --prefix sim/web run build   # -> sim/dist/index.html
```

## Checks

- `ctest --test-dir sim/out/host` (plant, scheduler, smoke, determinism).
- `node sim/web/test/run.mjs` (page smoke test) and `node sim/qa/run_qa.mjs` (visual QA; writes
  `sim/qa/REPORT.md`, screenshots in `sim/qa/screenshots/`). Both use headless Chromium via file://
  (`PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm --prefix sim/qa install` once; set the Chromium path in the scripts).

Layout audit: `node sim/qa/layout_audit.mjs` (needs `sim/dist/index.html` built; set `CHROME_PATH` to
override the Chrome location). It walks every screen of the firmware UI plus the Pulse Tune, calibration,
grind and warning-icon flows, and checks each piece of text and each control for these defects:
truncated, word-split, clipped-bottom, sticks-out-sideways, sticks-out-vertically, into-padding,
screen-edge, overlap and needs-scrolling. It takes about 5 minutes, writes PNGs to
`sim/qa/out/layout/` and exits non-zero on any defect.

## The page

Dashboard: live weight/flow/error/relay traces against the plant's truth, controller state diagram,
mass balance, plant parameter sliders, fault and action buttons, run history, CSV/log export, speed
0.25x-20x with pause and 20 ms steps. The firmware's own LVGL UI is shown on a 280x456 canvas (click
it to touch) and as a texture on a generic 3D grinder (orbit camera, exploded view with labelled
controller board, HX711, load cell, relay, motor, burrs, chute).

## What is real and what is modelled

- Real: every file under `src/` except the Bluetooth manager, the BLE OTA handler and the network
  services (Wi-Fi, web server, API, provisioning, GaggiMate client), which are replaced by
  "radio absent" stubs (`sim/shim/stubs/comms_stubs.cpp`). The LVGL 9.5.0 library and the firmware's
  own `include/lv_conf.h` render the real UI.
- Faked platform (`sim/shim/`): Arduino core, FreeRTOS (cooperative, deterministic), ESP-IDF timers,
  RMT relay output, GPIO, the HX711 two-wire protocol, the FT3168 touch controller, the CO5300 panel,
  NVS/Preferences and LittleFS (in memory), task watchdog.
- Modelled (`sim/plant/`): relay, motor, single-dose burr chamber and run-dry, chute retention,
  transport delay, cup and platform, load-cell mechanics, HX711 cadence and noise, faults. Most
  parameters are placeholders (`sim/plant/ASSUMPTIONS_PLANT.md`); numbers describe the model.
- See `ARCHITECTURE.md` (design), `ASSUMPTIONS.md`, `FINDINGS.md`, `QUESTIONS.md`, `PROGRESS.md`.

## Scenarios

JSON files drive a scripted user (`sim/core/operator.cpp`): beans per dose, cup mass, purge handling,
number of grinds, and timed events (faults, cup actions, resets) anchored to boot, START, CONTINUE or
the first entry of a controller phase. Examples: `sim/scenarios/normal.json`,
`sim/scenarios/findings/*.json`, `sim/mc/scenarios/*.json`.

The out-of-beans prompt (`REFILL_CONFIRM`) is answered per `refill_action`: `stop` (default; taps STOP,
so older scenarios keep the old "No beans?" outcome), `continue` (adds `refill_g` of beans, then taps
CONTINUE) or `ignore` (the prompt times out after 5 minutes). Options: `refill_max`, `refill_lift_cup`,
`refill_empty_cup`, `refill_moved_action` (`continue` | `stop` for the "Cup moved?" dialog) and
`refill_jiggle_s` / `refill_jiggle_g` (knock the platform while pouring, so CONTINUE waits for the scale
to settle). Examples: `sim/scenarios/refill/*.json`. Summaries add `refill_prompts` and
`motor_refill_pause_s` (relay contact closed while at the prompt).

## Windows desktop UI simulator (pre-existing)

Its original README is kept verbatim in `sim/DESKTOP_SIMULATOR.md` (this file was the desktop
simulator's README before the digital twin was added).
