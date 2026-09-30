# Digital twin build progress

Starting commit (firmware diff gate baseline): 3430179a4cdc2d5914b55badb9c24bee2a565f50
Branch: claude/code-review-ui-improvements-9e89pk
Firmware dirs for the diff gate: `src/ include/ components/ partitions.csv platformio.ini`
Gate command: `git diff --stat 3430179a4cdc2d5914b55badb9c24bee2a565f50 -- src include components partitions.csv platformio.ini`

## Tier status

| Tier | Status |
|------|--------|
| 0 Recon, ARCHITECTURE.md, sim_api.h | done |
| 1 Native sim + Monte Carlo | done (6300-run report, reviewed) |
| 2 Browser dashboard (WASM) | built by web-builder, 23/23 headless checks, pending commit |
| 3 Firmware UI on virtual screen | works (native PNG dumps + page canvas), pending commit |
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

- Tier 1: plant model (plant-modeller subagent, reviewed, 19 tests); shims + scheduler + fibers;
  full firmware links and runs natively (-m32); scripted operator taps the real UI buttons;
  grindsim CLI (single, --batch with fork per run, reset via re-exec); task watchdog model;
  host tests (scheduler, smoke, determinism) + plant tests in CTest (22 pass).

## Next

- Tier 2/3 commit, then Tier 4.

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

- PlatformIO build: registry unreachable (see toolchain section). Not needed; fallback is zero seams.

## Decisions (continued)

- D7 Communications (bluetooth/manager.cpp, bluetooth/ota_handler.cpp, network/*.cpp) are not compiled; their
  classes are implemented in sim/shim/stubs/comms_stubs.cpp as radio absent / never connected / no transfer.
  data_stream.cpp and image_upload_handler.cpp are compiled from src.
- D8 Each run is a fresh process image (firmware globals are constructed once per process). Batch forks per run;
  a firmware reset re-executes grindsim with NVS, LittleFS and plant state carried over (sim_persist_export).
- D9 Firmware clocks (millis, micros, esp_timer, ticks) restart at 0 at each simulated boot; world time continues.
- D10 Trace never calls WeightSensor::get_display_weight (it mutates the display filter); only const getters.

## Toolchain versions and fallbacks used

- Host: g++ 13.3.0 (Ubuntu 24.04), g++-multilib installed via apt (needed `apt-get update` first).
- Emscripten: emsdk (first choice) cloned from GitHub, `emsdk install latest` -> emcc 6.0.10. Worked.
- Headless browser: Playwright Chromium already on the VM at /opt/pw-browsers/chromium-1194 (first choice).
- LVGL 9.5.0 source: git clone --branch v9.5.0 from GitHub.
- PlatformIO: pip install platformio 6.2.0; `pio run -e waveshare-esp32s3-touch-amoled-164` tested once:
  platform and Arduino-ESP32 3.3.2 downloaded from GitHub, then failed installing `platformio/tool-scons`
  from the PlatformIO registry (HTTPClientError). So the rule 1 exception does not apply: zero firmware changes.
  The downloaded framework was used as a read-only reference: CONFIG_FREERTOS_HZ 1000 and
  CONFIG_ESP_TASK_WDT_TIMEOUT_S 5 / PANIC 1 (framework-arduinoespressif32-libs/esp32s3/qio_opi/include/sdkconfig.h:1178,1097-1098),
  loopTask priority 1 on core 1 (framework-arduinoespressif32/cores/esp32/main.cpp:113).

## Firmware seams (rule 1 exception)

None.

## Log

- Monte Carlo subagent: 141 scenario files, 6300 runs, full run 6 min, deterministic (quick mode rerun diffed identical by lead).
  Lead corrected three interpretation statements (dry-run rule scope, no stuck-value check, bump mechanism / MAX_PULSES is a
  completed result) after reading the source.
- Web-builder built Tier 2 + 3 in parallel while the Monte Carlo ran (decision: the WASM module and page do not change Tier 1;
  Tier 2/3 committed only after Tier 1).
