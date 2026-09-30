You are running an unattended overnight build in a Claude Code cloud session on this repo. Nobody
will answer questions until morning. Do not end your turn until sim/DONE exists (see "Done"). If
you are about to stop without it, re-read sim/PROGRESS.md and start the next unfinished item.

## Step 0

Save this entire prompt verbatim to sim/BRIEF.md, create sim/PROGRESS.md, then commit and push.
Re-read both whenever your context has been compacted or you lose track.

## Goal

Build a local, offline digital twin of this firmware (a fork of jaapp/smart-grind-by-weight): the
real firmware code, not a reimplementation, running against a physics model of the grinder and
load cell. The user downloads one file, sim/dist/index.html, and double-clicks it on Windows.
Alongside it, build a native Monte Carlo harness that stress-tests the grind controller.

The user single doses 18 g of beans at a time. Default target is 18.0 g, configurable.
This prompt adds to the repo's CLAUDE.md. For anything under sim/, this prompt governs.

## Environment (Anthropic-hosted cloud VM)

- Ubuntu 24.04 x86_64, roughly 4 vCPU, 16 GB RAM, 30 GB disk. Preinstalled: Python and pip,
  Node 22, GCC, Clang, CMake, Ninja, Docker, git.
- Network is an allowlist: package registries (npm, PyPI, apt), GitHub, Docker Hub,
  storage.googleapis.com. General internet is blocked. Test reachability before planning
  around a tool.
- Commands time out after 2 minutes by default (10 max) and are then backgrounded. Run long
  builds in the background and poll.
- The VM is reclaimed if the session goes idle. Push after every commit; unpushed work is lost.
- git push only works to this session's current branch. Stay on it. Never force-push.

## Hard rules

1. Zero changes to firmware sources (src/, include/, components/, and any other firmware dirs
   you find in recon). Compile the unmodified sources against fake Arduino / ESP-IDF / FreeRTOS /
   HX711 / display / touch / BLE / NVS implementations under sim/shim/. Gate:
   `git diff --stat <starting commit> -- <firmware dirs>` must be empty. Single exception: if
   `pio run -e <env>` works in this VM (test once; PlatformIO's registry may be unreachable),
   minimal mechanical seams are allowed, each listed in PROGRESS.md with its reason, and the
   PlatformIO build must pass after each one.
2. Deterministic virtual time. All time (millis, micros, delay, vTaskDelay, esp_timer, etc.)
   comes from a sim clock. Same seed and params give byte-identical output. Native runs as fast
   as possible; the browser runs at 1x with speed control from 0.25x to 20x, pause and single-step.
3. Browser build is single-threaded: no pthreads, no SharedArrayBuffer (a file:// page cannot be
   cross-origin isolated). Run firmware tasks on a cooperative scheduler, the same one natively
   and in WASM. Asyncify or emscripten fibers are acceptable.
4. The page works offline from file://: one HTML file, WASM embedded (emscripten
   -sSINGLE_FILE=1), all JS and CSS inlined (three.js and anything else via npm, bundled with
   esbuild). No CDN, no server, no network at runtime.
5. Never ask the user anything. Log each question in sim/QUESTIONS.md with the conservative
   choice you made, then continue.
6. No invented facts. Every plant-model default cites upstream code or docs (file and line) or
   is a placeholder in sim/ASSUMPTIONS.md with rationale and sweep range.
7. Visuals use a generic stylised grinder built from primitives. Do not replicate the Eureka
   Mignon's industrial design, branding, wordmarks or logos.
8. Timebox blockers. If the same blocker survives two distinct attempts or about 90 minutes,
   take the fallback, log it in PROGRESS.md, and move on.

## Toolchain fallbacks (record which one worked)

- Emscripten: emsdk (clone from GitHub; SDK downloads from storage.googleapis.com), then the
  emscripten/emsdk Docker image, then apt emscripten.
- Headless browser: any Chrome or Chromium already on the VM, then Chrome for Testing via
  `npx @puppeteer/browsers install chrome@stable`, then Playwright's own browser download.
  Drive it with Playwright (executablePath) or Puppeteer. If none works, skip visual QA, log it,
  and still build the page.
- JS deps via npm. Python plotting via pip.

## Recon first (you, not a subagent)

Read CLAUDE.md, README, docs/, platformio.ini and all firmware sources. Record in PROGRESS.md:
framework and PlatformIO envs; UI library, display and touch drivers, native resolution and
colour format; task and threading model; where the grind controller and state machine live and
their constants; what this fork changed relative to upstream. Then write sim/ARCHITECTURE.md
and sim/sim_api.h (boundary between shims and plant model, and between WASM and JS). Commit and
push before starting any subagent.

## Tiers

Finish, gate, commit and push each tier before starting the next. A partial night must leave a
working lower tier.

Tier 1, native sim and Monte Carlo (must have):
- CMake host build of firmware, shims and plant model under sim/host/.
- CLI along the lines of `grindsim --seed N --target 18.0 --params p.json --out run.csv`, plus
  `--batch N` writing one summary row per grind.
- sim/reports/montecarlo.md with SVG charts: final error against the firmware's own tolerance,
  overshoot, pulse count, grind time, and every failure case with seed and params to reproduce.
- Scenarios include single-dose run-dry (loaded mass slightly above and below target, so the
  grind can end because the burrs run empty rather than because the controller stopped) and
  the fault list below.
- Fallback: build only the controller and state-machine sources with shims.

Tier 2, browser dashboard running the real controller in WASM: live weight and flow traces,
motor and relay state, controller state diagram with the active state highlighted, target and
tolerance bands, plant parameter sliders, fault buttons, seed, speed control, run history,
CSV export.

Tier 3, the firmware's actual UI on a virtual screen: render into a canvas at native
resolution, pointer events drive the touch shim. LVGL: hook display flush. Otherwise shim the
display framebuffer. Fallback: a clear "UI not emulated" panel, keep Tier 2.

Tier 4, 3D twin and polish: three.js scene with the stylised grinder, hopper bean level, grounds
particle stream proportional to simulated flow, cup filling, the virtual screen as a texture
(interactive via raycast or mirrored in a side panel), orbit camera, exploded view showing
controller board, load cell and relay. Visual QA suite. sim/README.md.

## Plant model (sim/plant/, portable C, shared by host and WASM)

- Motor: relay on and off latency, spin-up, coast-down after power-off (still grinding), stall.
- Grind path: transport delay burrs to cup, flow vs grind setting and bean properties, flow
  noise and clumping, chute retention, flow taper as the burr chamber empties, run-dry.
- Load cell and HX711: 10 and 80 SPS, noise, drift and creep, quantisation, gain error, motor
  vibration coupling, impact spikes, cup placement and removal steps, tare.
- Seedable RNG; JSON parameter schema with sweep ranges.
- Unit tests per component, including mass conservation (loaded = in cup + retained + in flight
  + left in burrs).

## Faults to inject

Load cell disconnect, stuck reading, noise burst; relay stuck on and off; cup removed
mid-grind; cup bumped; motor stall; beans run out early; reset mid-grind if state persists.
Record what the firmware does in sim/FINDINGS.md, observation separate from interpretation.
Tag SAFETY where the motor runs without a valid weight signal or beyond a sane time limit.
Findings only; do not fix the firmware.

## Delegation

You (Opus) own recon, architecture, sim_api.h, shims, scheduler, toolchains, integration, gates,
and review of every subagent result before commit. Delegate bounded work to general-purpose
subagents, always passing model "sonnet" on the Agent call. Give each its role, the relevant
sections of sim/BRIEF.md, sim/ARCHITECTURE.md, the directory it owns, an acceptance test, and
the instruction to write only inside that directory and report assumptions and interface change
requests back to you. Run independent ones in parallel.

- plant-modeller, sim/plant/: C11, no deps, no threads, no wall clock, dt-stepped, seeded RNG,
  unit tests passing, parameter table marking each value as sourced or placeholder.
- montecarlo, sim/mc/ and sim/reports/: batch runs via grindsim, SVG charts, one-command rerun,
  reproducible seeds, SAFETY tags.
- web-builder, sim/web/: dashboard, virtual screen canvas, three.js scene, bundled into one
  offline HTML with the SINGLE_FILE glue; no controller logic in JS.
- visual-qa, sim/qa/: open sim/dist/index.html via file:// headless, fail on console errors,
  scenarios (normal 18.0 g, run-dry, cup removed, 20x), screenshots saved and viewed, defects
  reported with severity.

## Gates (run the relevant ones before each commit)

- Firmware diff empty (or PlatformIO build passes under the rule 1 exception).
- Host unit tests pass.
- Same seed twice gives an identical CSV hash.
- WASM build succeeds; headless browser loads the page via file:// with no console errors and
  completes a scripted grind; view the screenshots yourself before calling a visual tier done.

## State files (the session may compact; these are your memory)

- sim/PROGRESS.md: tier status, done and next, decisions, blockers with attempts, toolchain
  versions and which fallbacks were used.
- sim/ARCHITECTURE.md, sim/ASSUMPTIONS.md, sim/FINDINGS.md, sim/QUESTIONS.md.
- sim/MORNING.md, rewritten after every tier: what works, how to rebuild, how to get the page
  (open sim/dist/index.html on this branch on GitHub, download the raw file, double-click), what
  is broken, top findings, next steps. One screen.
- Build output dirs gitignored; sim/dist/index.html committed.

## Done

Create sim/DONE, commit and push only when Tier 4 is complete with gates passing, or when no
further progress is possible (explain why in MORNING.md). Then end your turn.
