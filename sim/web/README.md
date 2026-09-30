# sim/web: dashboard and virtual screen

Builds the single offline page `sim/dist/index.html`: the real firmware (WebAssembly from
`sim/out/wasm/grindtwin.js`, wasm embedded) running against the plant model, with a virtual touch screen,
live traces, controller state diagram, plant parameter sliders, fault and action buttons, run history, CSV
export and a firmware log. No controller logic is in JavaScript: the page presents state and forwards inputs.

## Build

```
sim/wasm/build.sh                      # once (or after firmware/plant changes): makes sim/out/wasm/grindtwin.js
npm --prefix sim/web install
npm --prefix sim/web run build         # writes sim/dist/index.html
```

`build.mjs` bundles `src/main.js` with esbuild and inlines CSS, the bundle and `grindtwin.js` into
`index.template.html`. It fails if the page's own code or template references an `http(s)` URL or uses
`fetch`/`XMLHttpRequest`/workers. The emscripten module carries one URL-like string from firmware data
(`http://` followed by text); it is never fetched. Raw NUL bytes of the module's binary string are written as
U+FFFD (what an HTML parser would do; the glue's decoder maps it back to 0), so the file contains no NUL.

Open `sim/dist/index.html` by double-click (file://). `three` is listed as a dependency for the later 3D tier but
is not imported yet, so it is not in the bundle.

## 3D twin

`src/scene3d.js` builds a generic stylised single-dose grinder from primitives (boxes, cylinders, spheres; no product
design, branding or logos) with three.js bundled by esbuild. It is driven only by the state snapshot:

| Visual | State field |
|--------|-------------|
| bean level in the hopper (instanced beans) | `m_hopper_g + m_burr_g` |
| burr and motor shaft rotation | `motor_speed` |
| grounds stream (max 400 particles, rate proportional to flow, none at 0) | `flow_cup_gps` |
| cup present / lifted away | `cup_present` |
| grounds level in the cup | `m_cup_g` |
| grounds lying on the bare platform | `m_platform_g` |
| relay command LED / relay contact LED | `relay_pin` / `relay_contact` |
| platform deflection (exaggerated) | `scale_signal_g` |
| display texture (the 2D canvas, updated when dirty), brightness, power | framebuffer, `brightness`, `display_on` |

Orbit camera (drag, wheel, right-drag), Reset view, and an Exploded view that animates the parts apart and labels the
controller board (ESP32 display module), HX711 board, load cell bar, relay, motor, burrs, chute, hopper and platform.
A press on the 3D screen is raycast to panel pixels and forwarded like a tap on the 2D canvas. Rendering runs at most
about 30 fps, backs off when a frame is expensive, and stops while the card is collapsed or off-screen. Without WebGL
the card shows a message and everything else keeps working.

## Test

```
PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm --prefix sim/web install
node sim/web/test/run.mjs [--chrome /path/to/chrome]
```

Opens the built page with an installed Chromium (default `/opt/pw-browsers/chromium-1194/chrome-linux/chrome`, launched
with `--use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader` so WebGL works headless),
fails on console errors/warnings, page errors or any network request, runs a manual tap on the Play button, Auto
grinds at mixed speeds and at 20x, compares the two final records (determinism), exercises step, pause, power
cycle, fault toggle, parameter slider, CSV/log downloads. Screenshots and downloads go to `test/out/` (gitignored).

## Source map

| File | Purpose |
|------|---------|
| `src/twin.js` | wrapper over one module instance (cwrap calls, framebuffer to RGBA, persistent-state export/import) |
| `src/main.js` | render loop, virtual-time budget, touch forwarding, world and power-cycle handling |
| `src/trace.js` | parses the 10 ms CSV trace into typed arrays, keeps raw text for export |
| `src/plots.js` | canvas plots (min/max decimation, hover read-out) |
| `src/diagram.js` | SVG state diagram (phases and edges read from the controller sources) |
| `src/panels.js` | masses, actions, faults, parameter sliders, history table, log, downloads |

## Behaviour notes

- Speed: each animation frame runs `speed x wall time` of virtual time, at most about 11 ms of wall time per frame;
  the backlog is dropped if the machine cannot keep up and the achieved ratio is shown next to the clock.
- Seed and target apply when a world is created (Boot / Restart world); the target lives in the firmware's
  preferences. Beans per dose and purge action apply to the next Auto grind. "start paused" holds a new world at
  power-on, which makes a scripted run start at exactly the same virtual time (used by the determinism test).
- A tap is held for at least 70 ms of virtual time (the firmware needs >= 50 ms). Tapping while paused advances
  virtual time by about 0.3 s so the firmware can react.
- Every Boot / Restart creates a new module instance (firmware globals are constructed once per instance). A
  firmware restart request (or Power-cycle firmware) exports the persistent blob, creates a new instance, imports
  it and boots; the trace continues without a second header, and earlier run-history rows are kept.
- Trace memory is bounded (240000 rows, about 40 min of virtual time); the oldest rows are dropped after that.
