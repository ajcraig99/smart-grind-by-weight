# Digital twin build progress

Starting commit (firmware diff gate baseline): 3430179a4cdc2d5914b55badb9c24bee2a565f50
Branch: claude/code-review-ui-improvements-9e89pk
Firmware dirs for the diff gate: `src/ include/ components/ partitions.csv platformio.ini`
Gate command: `git diff --stat 3430179a4cdc2d5914b55badb9c24bee2a565f50 -- src include components partitions.csv platformio.ini`

## Tier status

| Tier | Status |
|------|--------|
| 0 Recon, ARCHITECTURE.md, sim_api.h | done |
| 1 Native sim + Monte Carlo | in progress |
| 2 Browser dashboard (WASM) | not started |
| 3 Firmware UI on virtual screen | not started |
| 4 3D twin + polish + visual QA | not started |

## Recon (Step "Recon first")

Framework and envs (platformio.ini):
- Arduino on ESP32-S3 via the pioarduino platform 55.03.32 (Arduino core 3.x on ESP-IDF 5.x).
- Envs: `waveshare-esp32s3-touch-amoled-164` (default, V1 panel), `-debug`, `-v2`
  (`HW_DISPLAY_VARIANT_V2=1`), `-mock` (`DEBUG_ENABLE_LOADCELL_MOCK=1`, uses the in-firmware
  MockHX711Driver). The twin builds the default V1 env configuration with the real HX711 driver.
- Libraries: lvgl 9.5.0, GFX Library for Arduino 1.6.7, esp32-flashz, AsyncTCP 3.4.9,
  ESPAsyncWebServer 3.9.1, WebSockets 2.7.2, Improv 1.2.6; BLE via the Arduino-ESP32 BLE library.

UI, display, touch:
- LVGL 9.5.0, config `include/lv_conf.h`: LV_COLOR_DEPTH 16 (RGB565), LV_OS_NONE, builtin malloc
  with a 512 KB pool from `heap_caps_malloc(MALLOC_CAP_SPIRAM)`, refresh period 16 ms.
- V1 display: CO5300 AMOLED over QSPI through Arduino_GFX (`Arduino_ESP32QSPI` + `Arduino_CO5300`),
  flushed from `DisplayManager::display_flush_cb` with `draw16bitBeRGBBitmap` (LV_COLOR_16_SWAP=1
  when SMART_GRIND_SIM is not defined). V2: SH8601 via esp_lcd.
- Native resolution 280 x 456 portrait (`HW_DISPLAY_WIDTH_PX/HEIGHT_PX`, src/config/hardware.h:76-77),
  RGB565, partial render mode with a 280 x 40 row buffer (V1), rounder forces full-width areas.
- Touch: FT3168 at I2C 0x38 on SDA 47 / SCL 48, polled with the IDF i2c_master driver
  (`i2c_master_transmit_receive` of register 0x02, 5 bytes) from `TouchDriver::update()` each UI frame.

Task and threading model (src/tasks, src/config/system.h:26-45):
- Arduino `setup()`/`loop()` (loopTask, core 1); `loop()` runs network/web/firmware-validation and
  OTA suspend logic every 10 ms.
- TaskManager creates five FreeRTOS tasks, all `vTaskDelayUntil` loops:
  WeightSampling (core 0, prio 4, 20 ms), GrindControl (core 0, prio 3, 20 ms),
  UIRender (core 1, prio 2, 16 ms; drains grind UI events, UIManager::update, touch, lv_timer_handler),
  Bluetooth (core 1, prio 3, 20 ms), FileIO (core 1, prio 1, 100 ms; drains controller flash/log queues).
- Cross-task: GrindController serialises with a std::recursive_mutex; UI events via a latest-snapshot
  mailbox under portMUX; FreeRTOS queues for flash ops and log lines.
- Motor: `Grinder` drives the relay pin with RMT (infinite loop for continuous, finite symbols for pulses),
  a 50 ms esp_timer dead-man stops continuous runs after 1000 ms without `keep_alive()`.
- HX711 is bit-banged (digitalRead/digitalWrite + delayMicroseconds) by `HX711Driver`; the firmware is
  built for 10 SPS and flags >40 SPS as INVALID_SAMPLE_RATE (src/hardware/WeightSensor.cpp:194).

Grind controller and state machine:
- `src/controllers/grind_controller.{h,cpp}` (phase machine, timeouts, guards),
  `weight_grind_strategy.cpp` (PREDICTIVE stop, pulse decisions), `time_grind_strategy.cpp`.
- Phases: `src/controllers/grind_events.h` enum GrindPhase (18 phases incl. PRIME, PRIME_SETTLING,
  PURGE_CONFIRM). UI state machine: `src/system/state_machine.h` (UIState, 12 states).
- Constants: `src/config/grind_control.h` - tolerance 0.03 g, timeout 60 s (manual 30 s),
  10 pulse attempts, flow detection 0.5 g/s, undershoot default 1.0 g, coast ratio 1.0 (0.7-1.5),
  prime 1.0 g / 5 s max, dry run 0.2 g in 5 s, settling tolerance 0.010 g std, motor latency 50 ms
  default (30-300), max pulse 250 ms above latency, motor settling 200 ms, pulse settling timeout 3 s,
  final settling timeout 5 s, purge re-tare threshold 0.5 g, pause max 300 s.
- `src/config/hardware.h`: relay pin 18 (V1), motor settling 500 ms, dead-man 1000 ms / 50 ms.
- Removal guard `src/controllers/net_weight_guard.h`: 3 consecutive samples below -0.9 x pre-tare vessel
  weight, or below -10 g without a reference.

Fork versus upstream (merge-base b4a0be6 with remote `community`, Clinteastman fork of jaapp):
19 commits, 76 files, +2662/-602 in src/include. Main changes: motor dead-man and boot pin hold,
dry-run stop ("No beans?"), purge prompt re-tare/keep-grounds logic, thread-safe scale readings and
stricter settling/tare/calibration, 40 g max dose, grind-screen tap guards, network request guard,
update authorization, remote-start opt-in, firmware rollback. See CHANGELOG.md "[Unreleased] - personal fork".

Existing `sim/` content (before this work): a Windows-only LVGL desktop simulator (sim/main.cpp,
sim/CMakeLists.txt, sim/platform/, *.ps1) with a scripted mock grind, plus three small host tests.
It is left untouched; the twin lives in new subdirectories.

## Done

- Step 0: BRIEF.md saved, PROGRESS.md created.
- Recon (above), ARCHITECTURE.md, sim_api.h.

## Next

- Tier 1: shims + scheduler + CMake host build, plant model (subagent), grindsim CLI, Monte Carlo (subagent).

## Decisions

- D1 Full-firmware build: compile real `src/main.cpp` (setup/loop), all tasks, controllers, hardware
  drivers and the LVGL UI against shims. Communications (BLE, Wi-Fi/web) are compiled where cheap;
  otherwise their firmware classes are stubbed in `sim/shim/stubs/` as "never connected" (see ARCHITECTURE).
- D2 ILP32 everywhere: native build uses `-m32 -msse2 -mfpmath=sse` so `long`/pointers are 32-bit and
  float maths is IEEE single, matching the ESP32 and wasm32.
- D3 SMART_GRIND_SIM is NOT defined: it selects the Windows LVGL backend and removes production checks.
- D4 HX711 emulated at pin level (DOUT/SCK) so the unmodified bit-banged driver runs.
- D5 Relay input to the plant is the emulated GPIO/RMT pin level of HW_MOTOR_RELAY_PIN.
- D6 src/config/git_info.h is a gitignored file generated by the firmware build; the sim build creates
  a minimal one if missing (it cannot be overridden: quoted includes search the including dir first).

## Blockers (with attempts)

None yet.

## Toolchain versions and fallbacks used

- Host: g++ 13.3.0 (Ubuntu 24.04), g++-multilib installed via apt (needed `apt-get update` first).
- Emscripten: emsdk (first choice) cloned from GitHub, `emsdk install latest` -> emcc 6.0.10. Worked.
- Headless browser: Playwright Chromium already on the VM at /opt/pw-browsers/chromium-1194 (first choice).
- LVGL 9.5.0 source: git clone --branch v9.5.0 from GitHub.
- PlatformIO: pip install platformio 6.2.0; `pio run` test result: (pending)

## Firmware seams (rule 1 exception)

None.
