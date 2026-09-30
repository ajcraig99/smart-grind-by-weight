# Digital twin architecture

The twin runs the unmodified firmware sources (`src/`, `include/`) against fake platform
libraries, on a deterministic cooperative scheduler, coupled to a physics model of the grinder,
grounds path, load cell and HX711. The same C/C++ code builds natively (Monte Carlo) and to
WebAssembly (the offline page).

```
 +--------------------------- firmware (unmodified src/) ---------------------------+
 | main.cpp setup()/loop()   TaskManager tasks   GrindController   UIManager (LVGL) |
 | WeightSensor -> HX711Driver (bit-bang)        Grinder (RMT + GPIO + esp_timer)   |
 +--------|-----------------------|-------------------|------------------|----------+
          | Arduino/ESP-IDF/FreeRTOS/NVS/LittleFS/Arduino_GFX/I2C/BLE/WiFi fakes      |
 +--------v------------------ sim/shim (C++) ---------------------------------------+
 | pins.cpp: GPIO + HX711 serial protocol   rmt.cpp: relay pin waveform             |
 | esp_timer.cpp  freertos.cpp  nvs/Preferences  littlefs (in-memory)  gfx (fb)     |
 +--------|-----------------------------------------------------|-------------------+
          | sim_api.h (C)                                       | framebuffer, touch
 +--------v---------- sim/core (C++) ----------------+    +------v------------------+
 | scheduler (fibers), virtual clock, runtime,       |    | host CLI (grindsim) or  |
 | operator (scripted user), trace/CSV, faults       |<-->| WASM exports -> JS page |
 +--------|------------------------------------------+    +-------------------------+
          | plant.h (C11)
 +--------v---------- sim/plant (C11) ---------------+
 | relay, motor, burrs/hopper, chute, cup, load cell,|
 | HX711 conversion cadence, RNG, faults             |
 +---------------------------------------------------+
```

## Directories

| Path | Owner | Contents |
|------|-------|----------|
| `sim/sim_api.h` | Opus | C boundary: plant <-> shims, runtime <-> host/JS |
| `sim/core/` | Opus | scheduler, fibers, clock, runtime, operator, trace, WASM exports |
| `sim/shim/include/` | Opus | fake headers named like the real ones (`Arduino.h`, `freertos/task.h`, ...) |
| `sim/shim/src/` | Opus | fake implementations |
| `sim/shim/stubs/` | Opus | stub implementations of firmware comms classes not compiled from `src/` |
| `sim/plant/` | plant-modeller | C11 plant model, params JSON schema, unit tests |
| `sim/host/` | Opus | CMake native build, `grindsim` CLI, host tests |
| `sim/mc/`, `sim/reports/` | montecarlo | batch runner, SVG charts, `montecarlo.md` |
| `sim/web/` | web-builder | dashboard, virtual screen, three.js scene, esbuild bundling |
| `sim/wasm/` | Opus | emscripten build script |
| `sim/qa/` | visual-qa | headless browser suite, screenshots |
| `sim/dist/index.html` | build output (committed) | single offline page |
| `sim/.deps/`, `sim/out/` | build (gitignored) | LVGL checkout, build trees |

The pre-existing Windows desktop simulator (`sim/main.cpp`, `sim/CMakeLists.txt`, `sim/platform/`,
`sim/*.ps1`, three `*_test.cpp`) is left as it was.

## Build configuration

- Firmware compiled with the default env's flags: `-DARDUINO_USB_CDC_ON_BOOT=1 -DBOARD_HAS_PSRAM
  -DLV_LVGL_H_INCLUDE_SIMPLE -DNO_GLOBAL_UPDATE -DFZ_NOHTTPCLIENT -DLV_CONF_PATH=include/lv_conf.h`,
  `HW_DISPLAY_VARIANT_V2=0`, `DEBUG_ENABLE_LOADCELL_MOCK=0`. `SMART_GRIND_SIM` is not defined
  (it selects the Windows LVGL backend and removes production checks).
- ILP32: native `-m32 -msse2 -mfpmath=sse`, wasm32 is ILP32 already. `unsigned long` is 32 bits like
  on the ESP32, so `millis()` wrap arithmetic and printf formats behave as on the device.
- LVGL 9.5.0 is compiled from source with the firmware's own `include/lv_conf.h`, unmodified.
- `-fno-gnu-unique` natively so the firmware shared object can be unloaded and reloaded
  (used for the reset-mid-grind fault).

## Virtual time and scheduling

- One global clock `sim_now_us` (uint64 microseconds). `millis()`, `micros()`, `esp_timer_get_time()`,
  `xTaskGetTickCount()` (1 tick = 1 ms, CONFIG_FREERTOS_HZ 1000) all derive from it.
- The world advances in 1 ms frames. In each frame: (1) the plant is stepped by 1 ms, (2) due
  esp_timer callbacks and RMT completions fire (ISR-like, never block), (3) every task whose wake time
  has arrived runs, highest FreeRTOS priority first, ties in creation order, until all are blocked.
- Firmware code executes in zero virtual time. Time advances only by blocking calls (`delay`,
  `vTaskDelay`, `vTaskDelayUntil`, blocking queue/semaphore waits, `yield`) or busy waits
  (`delayMicroseconds` advances the clock in place; crossing a millisecond steps the plant and
  fires timers without switching task, like a CPU-bound task that keeps the core).
- Preemption points: `xTaskCreate*` yields to a newly created higher-priority task; a task that polls
  `millis()`/`micros()` more than 20,000 times without blocking is treated as busy-waiting and the
  clock is advanced 1 us per further call (prevents deadlock on spin loops).
- Tasks are fibers: `ucontext` natively, `emscripten_fiber_*` (ASYNCIFY) in WASM. The scheduler is
  identical in both; only `sim/core/fiber_*.cpp` differs.
- Determinism: no wall clock, no threads, single seeded RNG stream per subsystem (plant has its own
  splitmix/xoshiro state). Same seed + params + action script give byte-identical CSV.
- Known deviations: both ESP32 cores are serialised; a std::recursive_mutex held across a blocking
  call would not exclude another fiber (same OS thread). None of the firmware's locked sections block
  in the shims (flash/NVS writes are instant in memory).

## Pins and peripherals

- GPIO: `pinMode/digitalWrite/digitalRead/gpio_set_level/gpio_set_direction/...` go to a pin table.
- HX711 (DOUT pin 3, SCK pin 2 on V1): the shim implements the serial protocol. DOUT is LOW when the
  plant reports a conversion ready; each SCK rising edge shifts the next of 24 bits (MSB first, two's
  complement) onto DOUT; after the 25th-27th pulse DOUT is released HIGH and the plant is told the
  sample was consumed. SCK HIGH for more than 60 us powers the chip down; SCK LOW powers it up.
  A disconnected module leaves DOUT on the ESP32 pull-down (LOW).
- Relay (pin 18 on V1): level is the OR of the plain GPIO output and the RMT waveform when the pin is
  routed to RMT (`gpio_set_direction` detaches RMT, as on the chip). RMT loop_count -1 holds HIGH until
  `rmt_disable`; finite transmissions hold HIGH for the sum of HIGH half-symbol durations, then LOW,
  then call `on_trans_done`.
- esp_timer: periodic/one-shot callbacks run from the frame loop at their due time.
- Display (V1): fake `Arduino_CO5300::draw16bitBeRGBBitmap` / `draw16bitRGBBitmap` write into a
  280 x 456 RGB565 framebuffer with a dirty rectangle; brightness and panel on/off are recorded.
- Touch: fake `i2c_master_transmit_receive` answers the FT3168 register 0x02 read from the virtual
  pointer state (x, y, pressed).
- NVS/Preferences and LittleFS are in memory, serialisable (persist across the simulated reset).

## Plant model (sim/plant)

C11, no dependencies, no threads, no wall clock, dt-stepped, seeded RNG. Interface in `sim_api.h`
section "plant". Inputs per step: relay command level, HX711 SCK power-down level. Outputs: all masses,
flows, motor speed, relay contact state, cup presence, load-cell force as grams, and the HX711 state
(`ready`, `code`, `connected`, `powered`). Every default value is either cited (file:line) or listed in
`sim/ASSUMPTIONS.md` with rationale and sweep range. Mass conservation:
`loaded = cup + chute_retained + in_flight + burr_chamber + hopper + spilled` at every step.

## Operator and scenarios

The firmware is driven only through its real inputs: touch on the virtual screen (the operator taps
the production buttons at their on-screen coordinates), cup and beans on the plant, and faults.
The operator never calls controller methods. It observes UI state through read-only accessors
(`state_machine`, `grind_controller` globals) to know when to tap. Scenarios (JSON) list timed actions.

## Runtime API (host and JS)

`sim_api.h` section "runtime": create/boot, run for N ms, read a state snapshot, push touch, read the
framebuffer, set plant parameters, inject faults and user actions, read log lines and CSV rows,
export/import persistent state (reset). WASM exports the same functions with `EMSCRIPTEN_KEEPALIVE`;
JS never contains controller logic, only presentation and input.

## Outputs

- `run.csv`: one row per 10 ms of virtual time (columns in `sim/core/trace.cpp`).
- Batch summary CSV: one row per grind (seed, scenario, params hash, result, final weights, errors,
  pulses, times, SAFETY flags).
- Log: firmware `LOG_BLE`/Serial output with virtual timestamps.
