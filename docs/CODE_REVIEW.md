# Code, UI and Hardware Review

> **Baseline note (2026-09-27):** this review was written against upstream commit `afdacc8`. After it was written, this branch was rebased onto the community fork (`Clinteastman/smart-grind-by-weight`, commit `b4a0be6`), as chosen in section 2 (option A).
> - All file:line references below refer to `afdacc8`, not the current code.
> - Findings that section 2 marks as fixed in the community fork no longer apply.
> - The status of every finding on this branch is in [REVIEW_STATUS.md](REVIEW_STATUS.md).

**Scope:** upstream `jaapp/smart-grind-by-weight` `main` at commit `afdacc8` (this fork's starting point, as of 2026-09-27).

**Covers:** firmware (`src/`, `include/lv_conf.h`, `platformio.ini`, `partitions.csv`), host tooling (`tools/`), docs, hardware choices, and the touchscreen UI.

**Method:** four parallel reviews (grind control; hardware and sampling; UI/UX; BLE/OTA/logging/tasks). I then re-read the cited code for every Critical and High finding myself. I cross-checked the results against:
- upstream's open issues;
- the pinned framework's `sdkconfig.h`;
- the ESP-IDF v5.5 RMT driver source;
- the maintained community fork (section 2).

**Not done:**
- No firmware build: this cloud environment blocks `api.registry.platformio.org`.
- No hardware testing.
- Every runtime claim below is inferred from code unless stated otherwise.

**Confidence labels:**
- **Verified**: follows directly from the cited lines, or from cited framework source.
- **Plausible**: likely, but needs a bench or hardware check.
- **Uncertain**: depends on something I could not confirm here.

The items marked [TO CONFIRM] and every Assumption are collected in section 9.

---

## 1. Summary

1. **The motor can keep running with nothing managing it.** There are five ways this can happen (F-01 to F-05). Two of them can leave it running indefinitely:
   - pressing STOP at the wrong moment during the purge (F-01);
   - a BLE firmware-update command arriving mid-grind (F-02).

   Continuous grinding is an infinite hardware (RMT) loop, so anything that stalls or suspends the control task leaves GPIO 18 high.
2. **Accuracy bugs:**
   - Correction pulses longer than about 65 ms come out at the wrong length. The relationship between requested and actual length is not even monotonic (F-06).
   - Settling and tare decisions use only 2-5 samples (F-17, F-18).
3. **Bugs users have already reported upstream, now root-caused:**
   - The purge amount never saves: its NVS key is 16 characters, one over the limit (F-07, upstream #140).
   - "Err: neg wt" aborts come from a single sample (F-08, upstream #140, #141, #156).
4. **Security:** any BLE client in range can flash firmware while Bluetooth is on (F-09).
5. **Build:** `-Ofast` makes the compiler delete the firmware's NaN checks (F-12).
6. **Hardware:**
   - Waveshare now ships a second board revision. This firmware can't drive its display, and on that board GPIO 18 is shared with the touch interrupt (section 3.1).
   - Add a pull-down on the motor line.
   - Keep the ESP32-S3; a faster CPU won't fix the UI speed (3.6).
   - The HX711 is fine for a first build (3.4).
7. **UI:** the round 7.8 mm GRIND/STOP/OK button is the main hazard.
   - A swipe that starts on it starts a grind (U1).
   - A double tap on STOP or OK starts another grind (U2).
   - The result isn't kept on screen (U7).
   - Nothing protects the AMOLED from burn-in.

   Section 6.3 proposes a redesign built around a full-width bottom action bar.
8. **Decision needed before any code changes:** which codebase to build on (section 2).

---

## 2. Decision needed: which codebase to build on

A community-maintained fork, [Clinteastman/smart-grind-by-weight](https://github.com/Clinteastman/smart-grind-by-weight), is actively developed. Its latest release is v1.5.9, dated 2026-09-06 in its `CHANGELOG.md`, and I looked at `main` at commit `b4a0be6`. It was announced on upstream PR #145.

| | This fork (upstream v1.4.0) | Community fork (v1.5.9) |
|---|---|---|
| Firmware size (`src/**/*.{cpp,h}`) | ≈22.5k lines | ≈29.8k lines |
| Waveshare board revisions | original only | original + newer (separate build targets) |
| Connectivity | BLE | BLE, Wi-Fi web UI, Wi-Fi OTA, mDNS, Home Assistant local push |
| Remote motor start over the network | no | yes: web and Home Assistant start/stop, via the controller (changelog 1.5.1/1.5.3). **Not reviewed.** |
| F-01 STOP race | present | fixed: controller mutex (`src/controllers/grind_controller.h:71`) |
| F-02 OTA mid-grind | present | fixed: OTA rejected while the controller is active (`src/bluetooth/manager.cpp:699`, `ota_handler.cpp:123`) |
| F-03 stale/failed scale mid-grind | present | changelog 1.5.8 says fixed (not code-checked) |
| F-04 window buffer overflow | present | fixed (`circular_buffer_math.cpp:74-85`) |
| F-05 infinite RMT motor loop | present | **still present** (`src/hardware/grinder.cpp:82`) |
| F-06 pulse length encoding | present | fixed (`grinder.cpp:152-193`) |
| F-07 purge amount key | present | fixed (key `purge_amount_g`) |
| F-08 single-sample neg-wt abort | present | fixed (removal checked against the pre-tare weight, with persistence) |
| F-09 unauthenticated BLE OTA | present | no pairing or auth found by grep |
| F-12 `-Ofast` | present | **still present** (`platformio.ini:20`) |
| F-13 no dry-run abort | present | none found by grep |
| Desktop simulator and CI | no | yes (Windows simulator, V1/V2 CI builds) |

"Fixed" above means I saw the fix in the fork's code. "Changelog says" means I only read the claim.

- **Option A:** rebase your fork onto the community fork. I would then review what it adds (network API, web server, Home Assistant), and apply the remaining fixes and the UI work on top.
- **Option B:** stay on upstream and fix everything here. This means a smaller codebase, but the newer Waveshare board would need porting, and fixes that already exist elsewhere would be re-implemented.
- **Option C:** stay on upstream and cherry-pick specific fixes from the community fork.

**My recommendation is A.** The newer-board display support alone is a substantial piece of work already validated on hardware (upstream PR #145, fork `docs/TROUBLESHOOTING.md`), and 6 of the 13 top findings are already fixed in its code: F-01, F-02, F-04, F-06, F-07 and F-08. Its changelog also claims a fix for F-03. Two caveats: it's roughly a third more code and adds a network attack surface, which I have not reviewed. If you already own the original board and don't want Wi-Fi, B is reasonable.

---

## 3. Hardware

### 3.1 Waveshare board revision: check before buying or wiring

Sources: upstream issue #144 and PR #145, and the community fork's `docs/TROUBLESHOOTING.md` and `docs/HARDWARE_INSTALLATION.md`. I have not seen either board.

| | Original board (this firmware) | Newer board (SKU 31197; the PCB may still say `Rev1.1`) |
|---|---|---|
| Display controller | CO5300 via Arduino_GFX (`src/hardware/display_manager.cpp:19`) | SH8601. The community fork found that Arduino_GFX left it black and used Waveshare's esp_lcd driver instead. |
| Display chip select | GPIO 9 (`src/config/hardware.h`) | GPIO 46 |
| QSPI clock / X offset | as coded | 40 MHz / 20 px |
| GPIO 18 | free; this firmware drives the motor with it | touch interrupt (TP_INT) with a pull-up. The community fork found it "can leak voltage into the grinder control input". |
| Motor / HX711 SCK in the community fork | GPIO 18 / GPIO 2 | GPIO 16 / GPIO 1 |

- **With this firmware on the newer board:** the display stays black. If the grinder is wired to GPIO 18, that pull-up may switch the motor on during reset, boot or flashing. That part is inference; it depends on the grinder's input circuit.
- **How to tell the boards apart:** don't trust the PCB text. The community fork's test is to flash Waveshare's V2 demo (`waveshareteam/ESP32-S3-Touch-AMOLED-1.64-v2`). If that lights the panel and the original CO5300 image doesn't, it's the newer board.

### 3.2 Motor-line fail-safe (recommended on either board)

- **Hardware:** fit a 10 kΩ pull-down from the motor-control line to GND at the Waveshare header. A small series resistor is optional. GPIO 18 is undriven during reset, the ROM bootloader, USB flashing and panic reboots. It is also undriven until after display init, around 0.3 s or more (F-33). General engineering practice; the Eureka board may already pull its transistor base down, which I can't tell from the docs.
- **Firmware:** drive the motor pin LOW as the first statement in `setup()`, and bound every motor-on command in hardware (F-05).

### 3.3 Power budget

- The Eureka's 5 V rail comes from a 78L05 with "<100 mA available" (`docs/eureka-specialita-reverse-engineering.md`).
- The ESP32-S3 (with BLE), the AMOLED and the HX711 all run from it. The 1000 µF capacitor in the parts list is there to ride through brownouts; upstream issue #154 asks whether it's needed and has no answer.
- **Hold-up estimate:** t = C·ΔV/I. Assuming I = 0.2-0.5 A and ΔV ≈ 1.4 V (5.0 V down to ~3.6 V regulator headroom [TO CONFIRM from the Waveshare schematic]), 1000 µF gives about 2.8-7 ms. That rides through short dips, not a sustained sag. Inference.
- **Recommendation:** measure the board current with a USB power meter at full brightness with BLE on. `src/main.cpp:41-56` already logs the reset reason, so check for BROWNOUT after motor starts. If the margin is thin:
  - use a lower AMOLED brightness (its power scales with lit pixels);
  - keep BLE off unless needed;
  - use a dedicated 5 V supply.

### 3.4 Load-cell ADC

- **Current setup:** the firmware requires the HX711 at 10 SPS and blocks startup at 80 SPS (`HW_LOADCELL_SAMPLE_RATE_SPS 10`, `src/config/hardware.h`; `docs/TROUBLESHOOTING.md`).
  - At 10 SPS the controller gets a new weight every 100 ms, plus up to 20 ms of polling delay. At 1.5-3 g/s of flow that's 0.15-0.3 g per sample.
  - The design compensates by undershooting and then pulsing.
- **What a faster ADC buys:** mostly speed (faster flow-start detection, shorter settle waits, fewer pulse cycles) and better latency estimates. Final accuracy comes from settled readings, where slower conversion is quieter. Inference.
- **Options** (datasheet figures are general knowledge [TO CONFIRM]):
  - **HX711 at 80 SPS** (RATE pin high; many modules hard-wire it low):
    - Hardware cost: none.
    - Noise per sample rises from about 50 nV to about 90 nV rms at gain 128.
    - Firmware work: poll faster than 12.5 ms or use a DOUT interrupt, retune the settle and filter windows, and remove the 80 SPS block. F-04 must be fixed first.
  - **NAU7802** (I2C, 10-320 SPS, on-chip excitation LDO). Requested upstream in #157.
    - `src/hardware/load_cell_driver.h` already abstracts the driver.
    - Its output is two's complement; `WeightSensor.cpp:718` only accepts 0..0xFFFFFF.
    - Put it on its own I2C port rather than the touch bus.
  - **ADS1232** (10/80 SPS, HX711-like protocol, lower noise): the smallest code change of the alternatives, but few ready-made modules. **ADS1220** (SPI, up to 2 kSPS) is the most work.
- **Recommendation:** build the first unit with the HX711 at 10 SPS, which the firmware is tuned for. Fix F-03, F-04, F-17 and F-18, measure noise with the motor running, and only then decide on a change. If you do change, the NAU7802 or ADS1232 are the candidates.

### 3.5 Load cell and mechanics

- Relative resolution comes from the calibration factors the docs quote (`docs/TROUBLESHOOTING.md`): about ±4400 counts/g for the 1 kg T70 and ±6580 counts/g for the 0.3 kg Mavin. So the 0.3 kg cell gives about **1.5×** the counts per gram, not the 3× its capacity ratio suggests. My inference is that the T70 has roughly twice the mV/V sensitivity.
- **Recommendation:** use the 1 kg T70, which the docs say was personally verified. It also handles a portafilter.
- Other points:
  - Mount the cell rigidly.
  - Keep the leads short and ground the shield. Upstream #141 (−0.1 g/s drift, leads over 20 cm) is consistent with wiring noise or creep, but that is Uncertain.
  - A module with separate analog VCC (5 V) and logic VDD (3.3 V) would raise the excitation voltage [TO CONFIRM for a specific module]. Never put 5 V logic on GPIO 2 or 3; the S3 is not 5 V tolerant.

### 3.6 "Faster CPU for the UI"

- The ESP32-S3 already runs at its 240 MHz maximum (`platformio.ini:12`).
- The UI pipeline is strictly serial (`src/hardware/display_manager.cpp`):
  - one 22.4 KB draw buffer in PSRAM (lines 43-49);
  - a memcpy into a 16-row internal staging buffer;
  - a polled, blocking SPI transfer (lines 112-152);
  - no second buffer, so rendering and transfer never overlap;
  - every dirty area widened to full width (lines 105-110).
- A faster MCU would only speed up the render step (inference).
- The fix is in software (F-38). It competes with BLE for internal RAM, though: upstream #128 reports 2 KB free with BLE on in v1.4.0, so measure the free heap first.
- The only faster Espressif part, the ESP32-P4, has no radio and would mean a full port.
- **Recommendation:** keep the ESP32-S3.

### 3.7 Motor drive

- The Eureka drives its own relay from the logic signal through an NPN transistor (`docs/eureka-specialita-reverse-engineering.md`), so it needs no relay or SSR.
- If you ever adapt this to a grinder that needs mains switching:
  - a zero-cross SSR rounds switching to half-cycles (10 ms at 50 Hz), which is comparable to these pulse lengths;
  - a random-turn-on SSR or the stock relay is better.
  - Auto-tune compensates for fixed delay, not jitter.

---

## 4. Firmware findings

Each entry gives:
- **Where:** file and lines;
- **What happens:** the failure scenario;
- **Confidence**;
- **Fix**;
- **Fork:** community-fork status (section 2).

### 4.1 Critical: the motor can run with nothing managing it

**F-01: STOP during the purge can restart the motor and leave it running**
- **Where:**
  - `src/ui/controllers/grinding_controller.cpp:221` → `GrindController::stop_grind()` at `src/controllers/grind_controller.cpp:236-256` (UI task, core 1);
  - PRIME handler at `grind_controller.cpp:367-370` (control task, core 0);
  - early return while IDLE at `:286`.
- **What happens:** `stop_grind()` stops the motor (`:239`), then discards the log buffer and prints, and only then sets IDLE (`:255`). If the 20 ms control loop runs in that gap while still in PRIME, it sees the motor off and starts it again (`:368-369`). Once the phase is IDLE, `update()` returns immediately, so the timeout and failsafes never run again. The motor keeps going while the UI shows Ready, until power-off or the next grind.
- **Related:** the same race at `:353-361` and `:420-422` keeps grinding with no active strategy until the 60 s timeout.
- **Confidence:** Verified (race logic). How often it happens is inference; plausibly a few percent of STOP presses during the first 1-2 s.
- **Fix:** make the control task the only code that touches the controller and grinder. Route start, stop, continue and pulse through the existing but unused `ui_to_grind_queue` (`src/tasks/task_manager.cpp:89`), or add a controller mutex. Add a per-cycle invariant: if the controller is not active but the grinder is on, stop it.
- **Fork:** fixed.

**F-02: A BLE OTA start mid-grind suspends control and leaves the motor running**
- **Where:** `src/bluetooth/manager.cpp:680-722` → `src/bluetooth/ota_handler.cpp:100-165`; watchdog set to 1800 s at `:137-142`; tasks suspended at `:147` → `src/tasks/task_manager.cpp:257-275`; also `src/main.cpp:176-188`.
- **What happens:**
  - There is no grind or motor check, and nothing stops the motor before the grind and weight tasks are suspended.
  - The UI switches to the OTA screen, which has no STOP button.
  - The motor runs for the whole transfer, typically minutes (inference).
  - The watchdog is never restored after an abort or failure, because `esp_task_wdt_reconfigure` has only one call site. So afterwards a hung control task goes unnoticed for 30 minutes instead of 5 s.
  - With no auth (F-09), any client in range can trigger this.
- **Confidence:** Verified.
- **Fix:**
  - Reject OTA START with a BUSY status unless the controller is idle, the motor is off, and no auto-tune or motor test is running.
  - Call `grinder.stop()` inside `suspend_hardware_tasks()`.
  - Restore the watchdog on every exit path, and add a 10-15 s no-data abort.
- **Fork:** fixed (the gating).

**F-03: A failed or disconnected load cell mid-grind is not detected**
- **Where:**
  - `src/hardware/hx711_driver.cpp:38` (DOUT set to `INPUT_PULLDOWN`), `:111-113`;
  - fault checked only at grind start (`grind_controller.cpp:115`);
  - `src/hardware/circular_buffer_math/circular_buffer_math.cpp:66-68` (falls back to the latest sample) and `:319-320` (standard deviation of 0 or 1 samples = 0, so "settled");
  - recovery code never called (`src/tasks/weight_sampling_task.cpp:344-367`).
- **What happens:**
  - **DOUT stuck high** (no samples): the weight freezes at its last value.
  - **DOUT open:** the pull-down makes it read LOW, which means "ready". Every 20 ms poll then returns 24 zero bits, which becomes `0x800000`. That is accepted as a valid sample at 50 SPS, and it triggers F-04.
  - Either way the motor either stops early or runs to the 60 s timeout and overfills.
- **Confidence:** Verified from code. The electrical behaviour is Plausible.
- **Fix:** each cycle while the motor is on, stop with an error if the newest sample is older than about 300 ms or any value is non-finite.
  - Flag these as faults: intervals much shorter than nominal, N identical readings, and saturation codes.
  - Confirm DOUT returns HIGH after each read.
  - Use `INPUT_PULLUP`, so an open line reads as "never ready".
- **Fork:** changelog 1.5.8 claims a 500 ms stale-scale stop.

**F-04: Stack buffer overflow when samples arrive faster than 10 SPS**
- **Where:** `circular_buffer_math.cpp:128-141` (buffer sized as `window × 10 SPS + 10`); `:74-95` (copies every sample in the window, unbounded).
- **What happens:** at 50 SPS (the F-03 fault) any window over 225 ms that goes through this copy overflows the stack. The copy is used by the smoothed-weight, standard-deviation and min/max functions. For example, the 500 ms settle window writes 10-11 extra `int32` values past the buffer, and longer windows write more. This lands on the 6-8 KB control or UI stack. The result is a stack-protector panic mid-grind (the reboot does stop the motor) or silent corruption (inference).
- **Confidence:** Verified.
- **Fix:** pass the buffer capacity into the copy and stop at it, and snapshot the buffer indices once per call.
- **Fork:** fixed.

**F-05: Continuous grinding has no hardware bound on motor-on time**
- **Where:** `src/hardware/grinder.cpp:70-81` (`loop_count = -1`, an infinite loop).
- **What happens:** the only limits are the controller's software timeout and the 5 s task-watchdog panic (`CONFIG_ESP_TASK_WDT_TIMEOUT_S 5`, `PANIC 1` in the pinned `sdkconfig.h`). Both fail if the control task is suspended (F-02) or stuck in a race (F-01).
- **Confidence:** Verified.
- **Fix:** keep the motor on with finite RMT bursts (for example 200-500 ms, both symbol halves HIGH), re-queued by the 20 ms loop (queue depth is already 4, `grinder.cpp:31`). If the loop stops, the motor stops within one burst. Also add an `esp_timer` cutoff inside `Grinder` as a second layer. Gap-free chaining needs a bench check.
- **Fork:** still present.

### 4.2 High

**F-06: Correction pulses longer than about 65 ms come out at the wrong length**
- **Where:** `src/hardware/grinder.cpp:157-179`. In ESP-IDF v5.5, `components/esp_driver_rmt/src/rmt_tx.c:555-556` treats `loop_count` as the total number of transmissions (`1` means a single one).
- **What happens:** the symbol that repeats on every loop contains the remainder too, so the remainder is repeated each loop. Requested → actual HIGH time, computed from the code:

  | Requested (ms) | 65 | 66 | 80 | 100 | 150 | 250 | 300 | 1000 |
  |---|---|---|---|---|---|---|---|---|
  | Actual (ms) | 65.0 | 33.2 | 47.2 | 68.9 | 155.1 | 320.4 | 302.9 | 1443 |

  This covers:
  - weight-mode correction pulses (latency plus up to 250 ms);
  - the time-mode "+" pulse (100 ms, `GRIND_TIME_PULSE_DURATION_MS`);
  - auto-tune pulses;
  - the 1 s Motor Test (`src/ui/controllers/menu_controller.cpp:614`).

  Short pulses cost extra cycles; long ones overshoot. The mock build skips the RMT path, so it cannot show this.
- **Confidence:** Verified (arithmetic plus IDF source). Confirm with a logic analyser on GPIO 18.
- **Fix:** transmit one flat symbol array once with no looping. A symbol with both halves HIGH covers 65.5 ms, so 1 s needs 16 of the 64 slots. Alternatively, end the pulse with an `esp_timer` one-shot.
- **Fork:** fixed.

**F-07: The purge amount is never saved**
- **Where:** key `"grinder_amount_g"` (16 characters) at `src/controllers/grind_controller.h:232`. `NVS_KEY_NAME_MAX_SIZE` is 16 including the terminator, so the limit is 15 characters (`nvs_flash/include/nvs.h:61` in the pinned IDF 5.5 libs). The write at `menu_controller.cpp:437` fails; the read at `grind_controller.cpp:131` returns the 1.0 g default.
- **What happens:** the slider shows the value you chose, but every grind purges 1.0 g. This matches upstream #140.
- **Confidence:** Verified.
- **Fix:** use a key of 15 characters or fewer, and check `put*` return values across the codebase.
- **Fork:** fixed.

**F-08: A single negative sample aborts the grind ("Err: neg wt")**
- **Where:** `grind_controller.cpp:551-567`. It uses the least-filtered weight (`:294`), with a 500 ms guard after each motor start (`grinder.cpp:208-214`).
- **What happens:** one 20 ms loop sample below −1 g aborts the grind. Motor torque reaction and vibration can cause a dip like that. Upstream #141 reports the abort "right when the motor torque kicks in"; #140 reports it when the motor starts; #156 reports random occurrences.
- **Confidence:** Verified (logic). The cause is Plausible.
- **Fix:** require the reading to persist (for example 200-300 ms, or N consecutive samples), and compare against the vessel weight measured before tare, so "cup removed" means something like weight < −0.5 × vessel weight.
- **Fork:** fixed.

**F-09: BLE firmware update and all BLE services are unauthenticated**
- **Where:** `src/bluetooth/manager.cpp:104-280` sets only property flags; there is no security or encryption setup anywhere in `src/`. BLE is on for 5 minutes after every boot (`main.cpp:112-114`, `bluetooth.h:57`) and never times out while a client stays connected.
- **What happens:** anyone in range with the public tooling can flash arbitrary firmware that controls a mains motor. A full update needs no knowledge of the current image, because the base is empty (`components/delta/delta.c:69-74`). Diagnostics (including an NVS dump), logs and the debug stream are also open.
- **Confidence:** Verified. That an attack needs physical proximity is an Assumption.
- **Fix, in proportion:**
  1. An on-device "Allow firmware update?" prompt, or an "arm update for 2 min" menu item, required before START is accepted.
  2. Passkey pairing with the key shown on the AMOLED, plus encrypted writes. NimBLE security is compiled in (`CONFIG_BT_NIMBLE_SECURITY_ENABLE 1`, `CONFIG_BT_NIMBLE_SM_SC 1`); Web Bluetooth pairing behaviour is [TO CONFIRM].
  3. Optionally, image signing with your own key.
- **Fork:** no auth found.

**F-10: A BLE data export takes over the screen mid-grind**
- **Where:** `src/ui/controllers/ota_data_export_controller.cpp:48-55` and `:117-126` switch to the OTA/export screen; READY is forced at the end. Neither checks grind state.
- **What happens:** running `grinder.py export` or `analyze` while grinding hides STOP (the physical button is removed in this mod, `docs/DOC.md:188`). The UI then drops to Ready while the controller may still be grinding.
- **Confidence:** Verified (no gating). Exact on-screen behaviour is Plausible.
- **Fix:** reply BUSY to file-list and file requests while the controller is active, and never change UI state from the export path during GRINDING.
- **Fork:** not checked.

**F-11: No re-tare after the purge confirmation**
- **Where:** `grind_controller.cpp:258-283` (`continue_from_purge` starts the motor with the pre-purge zero).
- **What happens:**
  - Grounds stuck to the cup shorten the dose by that amount.
  - A different cup shifts the dose by the difference in cup weight, or trips F-08.
  - Pressing CONTINUE before the cup is back sprays grounds for at least 500 ms.

  Purge is the default mode and runs on the first grind after boot and after the freshness interval (default 8 h).
- **Confidence:** Verified.
- **Fix:** after CONTINUE, go back through TARING → TARE_CONFIRM, and check that the cup is present against the pre-tare snapshot (`grind_controller.cpp:326`).
- **Fork:** not found by grep.

**F-12: `-Ofast` deletes the NaN/Inf checks**
- **Where:** `platformio.ini:20`. The guards affected are `src/hardware/WeightSensor.cpp:569` and `:766`, and `src/ui/screens/calibration_screen.cpp:204`.
- **What happens:** `-Ofast` implies `-ffinite-math-only`. With host GCC 13.3, `std::isnan(f)` compiles to `return false`. So a corrupted NaN calibration factor is accepted, every weight becomes NaN, both the target stop and the negative-weight stop are disabled, and only the 60 s timeout remains. `-Ofast` also allows store data races (`-fallow-store-data-races`), which makes F-31 worse.
- **Confidence:** Verified on the host compiler; Plausible on the target GCC 14.2 (check by disassembling `WeightSensor.o`).
- **Fix:** build with `-O2`, or add `-fno-fast-math`. Any `isfinite` fix elsewhere depends on this.
- **Fork:** still present.

**F-13: Nothing aborts a dry run (empty hopper or blocked chute)**
- **Where:** `grind_controller.cpp:373-381` (PRIME ends at its 5 s limit and carries on); `src/controllers/weight_grind_strategy.cpp:87-97` (no deadline for detecting flow).
- **What happens:** PRIME runs 5 s, then PREDICTIVE runs until the 60 s timeout: about 55 s of dry running.
- **Confidence:** Verified.
- **Fix:** abort with "No beans?" if PRIME delivers under about 0.2 g, or if PREDICTIVE confirms no flow within 3-5 s.
- **Fork:** none found by grep.

### 4.3 Medium

"Verified" means the cited code shows the defect; how often it bites is noted where it matters.

**F-14: pulse completion is read with `digitalRead()` on a pin the RMT driver owns**
- Location: `grinder.cpp:198`.
- The RMT driver enables only the output path (IDF `rmt_tx.c:343-353`), and the Arduino core warns that such reads "may return an inconsistent value" (`esp32-hal-gpio.c:187-191`, core 3.3.2).
- If the input path is off, every pulse is reported complete at the first 20 ms check.
- Fix: use `rmt_tx_wait_all_done(ch, 0)` or an `on_trans_done` callback.
- Confidence: Uncertain. Fork: fixed.

**F-15: race in the time-mode "+" pulse**
- Location: `grind_controller.cpp:1063-1068` (UI task).
- The phase switches to TIME_ADDITIONAL_PULSE before the pulse starts, so the control task can finish the phase immediately. The `grinding` flag then stays true, and the next grind's `start()` is skipped (`:353`). A time grind can then "succeed" with no coffee.
- Fix: F-01's single command path.
- Confidence: Verified logic; low probability.

**F-16: RMT encoder deleted and recreated from two tasks with no lock**
- Location: `grinder.cpp:57-68`, `101-105`, `126-137`. Callers are on the UI task (`grinding_controller.cpp:195`, `221`; `menu_controller.cpp:614`, `678`) and the control task.
- Consequences: possible double free; return codes from `rmt_*` are ignored.
- Fix: create one encoder in `init()`, add a mutex, and check the return codes.
- Confidence: Verified.

**F-17: settling is judged by standard deviation over only 2-5 samples**
- Location: `circular_buffer_math.cpp:258-261`, `319-320`; `weight_grind_strategy.cpp:128`, `168-170`.
- With 0 or 1 samples the check reads "settled".
- A slow trickle still reads as settled, simulated by re-implementing the code:
  - up to ~0.06 g/s over 500 ms;
  - up to ~0.1-0.14 g/s over 200 ms.
- That can use up the 0.03 g tolerance.
- Fix: also require a small slope and a minimum sample count.
- Confidence: Verified maths; the effect on real grinds is Plausible.

**F-18: tare waits ~1.9 s but uses only the last 250 ms (2-3 samples)**
- Location: `WeightSensor.cpp:723-733`; `grind_controller.cpp:348-352`.
- The offset is captured even if the scale wasn't settled, and is never retried.
- Fix: average over the whole tare window, and restart the tare if that window wasn't settled.
- Confidence: Verified.

**F-19: one-off UI events can be dropped**
- Location: `grind_controller.cpp:884-891`.
- One 10-slot queue carries 50 Hz progress updates plus the one-off events (COMPLETED, TIMEOUT, PURGE_CONFIRM). When it's full, events are dropped.
- Consequences: the UI can get stuck on the grinding screen, or the controller waits in PURGE_CONFIRM with no timeout.
- Fix: give one-off events their own queue, and carry progress as a latest value (`xQueueOverwrite`).
- Confidence: Verified (the drop path); the stuck-UI outcome is Plausible. Fork: changelog 1.5.8.

**F-20: Pulse Tune cancel only sets a flag**
- Location: `src/ui/controllers/autotune_controller.cpp:137-144`.
- The flag is processed only on the auto-tune screen, which the UI has just left.
- Consequences: the pulse isn't stopped, the log stays open, and Tune can't be restarted until reboot.
- Confidence: Verified (per the review). Fork: changelog 1.5.8.

**F-21: auto-tune search problems**
- Location: `src/controllers/autotune_controller.cpp:272-366`, `406`.
- The search re-tests points already known to fail.
- It rounds the result up by up to about 15 ms, which adds 0.015-0.045 g to every correction pulse.
- A result above 300 ms is silently not saved (`grind_controller.cpp:1113-1117`), yet the UI shows success.
- Confidence: Verified (per the review, simulated).

**F-22: no settling timeout in FINAL_SETTLING or the pulse phases**
- Location: `grind_controller.cpp:465-470`; `weight_grind_strategy.cpp:128-130`, `168-172`.
- On a noisy bench the grind ends as "Timeout:FINA" with no weight shown.
- Fix: after `GRIND_SCALE_SETTLING_TIMEOUT_MS`, fall back to the smoothed weight.
- Confidence: Verified.

**F-23: flash writes and blocking logs happen during grinding**
- Sources:
  - `src/tasks/file_io_task.cpp:297-307` writes and deletes `/test_access` every 30 s, even with logging off;
  - NVS writes at grind start (`src/logging/grind_logging.cpp:91-93`) and every 15 min for uptime (`main.cpp:155-174`);
  - `LOG_BLE` is a blocking `Serial.printf` (`src/config/logging.h:10`), with a 100 ms USB-CDC TX timeout (core 3.3.2 `HWCDC.cpp:48`).
- Why it matters: flash operations stall the cache on both cores, which delays the stop decision (general ESP-IDF knowledge). Estimate: assuming a sector erase stalls for about 45 ms [TO CONFIRM for this flash part], that's 0.045-0.135 g of extra coffee at 1-3 g/s, against a 0.03 g tolerance.
- Fix: defer writes while grinding, check the filesystem with `usedBytes()` instead, and queue logs from core 0.
- Confidence: Verified writes; the impact is Plausible.

**F-24: heavy work inside BLE callbacks, on core 0**
- Location: `manager.cpp:714` (erase), `732` (3 MB erase and patch apply), `819-822` (filesystem scans), `867` (`delay(500)`).
- The pinned `sdkconfig.h` sets `CONFIG_BT_NIMBLE_PINNED_TO_CORE 0`, which is the same core as grind control.
- Fix: queue the commands to the BLE task, as the diagnostics path already does.
- Confidence: Plausible impact.

**F-25: BLE state shared across tasks with no lock**
- Location: `manager.cpp:850-862`; `data_stream.cpp`.
- A disconnect during an export can close the file while the BLE task is reading it: a use-after-free.
- Confidence: Plausible.

**F-26: unsigned underflow in the session sort**
- Location: `src/bluetooth/data_stream.cpp:79`: `i < list_count - 1` with `list_count == 0`.
- This happens when files are counted but none parse. The loop then runs ~4.3×10⁹ times inside a BLE callback on core 0, and the watchdog reboots the device.
- Confidence: Verified; rare trigger.

**F-27: OTA rollback protection is effectively off**
- The bootloader has rollback enabled (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1`).
- But the Arduino core marks a new image valid before `setup()` unless `verifyRollbackLater()` returns true (core 3.3.2 `esp32-hal-misc.c:277-291`), and nothing in `src/` overrides it.
- Consequence: an image that crashes in `setup()` boot-loops, and recovery needs USB with the board inside the grinder.
- Fix: return true from `verifyRollbackLater()` and mark the image valid after 30-60 s of healthy running.
- Confidence: Verified.

**F-28: an interrupted OTA leaves the device in a bad state**
- The UI stays on the OTA screen.
- Touch stays disabled: `TouchDriver::enable()` has no callers.
- The `new_build_nr` and `new_fw_ver` keys are not cleared on abort. They are only checked and removed at the next boot (`ota_handler.cpp:363-403`), which then reports a spurious "update failed".
- Location: `ota_handler.cpp:119-128`, `211`; `ota_data_export_controller.cpp:37-46`.
- Confidence: Verified.

**F-29: values loaded from NVS are not validated**
- An unknown `grind_mode` leaves `active_strategy == nullptr` while PRIME and PREDICTIVE still run the motor (`grind_controller.cpp:190-196`, `357-361`, `441-445`).
- A NaN latency passes the range check.
- Fix: range and `isfinite` checks on load (after F-12), and treat "motor on with no strategy" as an error.
- Confidence: Verified logic; corruption is unlikely.

**F-30: `calibrate()` bit-bangs the HX711 from the UI task**
- Location: `WeightSensor.cpp:292-295`.
- The sampling task does the same on core 0, so the clocks can interleave and change the next conversion's gain or channel. The value read is also thrown away.
- Fix: delete those lines and wait for N new samples instead.
- Confidence: Verified; low probability.

**F-31: the sample buffer and tare flags are shared across cores with no synchronisation**
- Location: `circular_buffer_math.cpp:21-36`, `519-530`.
- The buffer is cleared from the UI task (`WeightSensor.cpp:309-310`) while samples are added on core 0.
- Fix: a spinlock around add and clear, a snapshot copy for readers, and `std::atomic` for the flags.
- Confidence: Verified race; rare.

**F-32: touch ACK checking is disabled, so NACK reads aren't rejected**
- Location: `src/hardware/touch_driver.cpp:57-61`, `96-106`; `DEBUG_SUPPRESS_TOUCH_I2C_ERRORS 1`.
- An idle bus reads 0xFF, which decodes as 15 touches at (4095, 4095). That is a phantom press, and it also keeps the screen from dimming.
- Fix: re-enable ACK checking and just silence the log, and reject touch counts above the controller maximum and out-of-range coordinates.
- Confidence: Plausible (general I2C behaviour).

**F-33: GPIO 18 is undriven until after display init**
- Location: `src/hardware/hardware_manager.cpp:6-10` (after the CO5300 delays); `main.cpp:63` (`LittleFS.begin(true)` may format first). Mock builds never drive the pin (`grinder.cpp:20-23`).
- Fix: see 3.2.
- Confidence: Uncertain (electrical).

**F-34: the UI can freeze with no recovery**
- `LV_ASSERT_HANDLER` spins forever (`include/lv_conf.h:423`), and neither the UI task nor core 1's idle task is watched.
- Fix: call `esp_system_abort()` and subscribe the UI task to the watchdog.
- Confidence: Verified.

**F-35: `tare()` and `calibrate()` block the UI task**
- They block for up to about 4 s and 12 s, from an LVGL timer (`WeightSensor.cpp:234-313`).
- Confidence: Verified.

**F-36: time arithmetic is not safe over long uptimes**
- The window functions use `now - window` (`circular_buffer_math.cpp:77-78` and others), which is not wrap-safe. For one window length after the 49.7-day `millis()` wrap they return stale values and "settled".
- `weight_grind_strategy.cpp:101` adds `millis()` to a float, which loses resolution with uptime (8 ms after 1 day, 64 ms after 7 days).
- Confidence: Verified.

**F-37: the grind timeout is 60 s in code but 30 s in the docs**
- Code: `GRIND_TIMEOUT_SEC 60` (`src/config/grind_control.h`), excluding purge-confirm time (`grind_controller.cpp:797-802`).
- Docs say 30 s: `CLAUDE.md`, `docs/DOC.md:538`, `580`, `docs/TROUBLESHOOTING.md`.
- With `USER_MAX_TARGET_WEIGHT_G 1000`, any dose above ~60-180 g always times out (at 1-3 g/s).
- Fix: decide the limit, or scale it with the target.
- Confidence: Verified.

**F-38: display pipeline performance (see 3.6)**
- Fix:
  - two DMA-capable internal-RAM buffers with an asynchronous flush;
  - a rounder that aligns to even pixels on both axes rather than forcing full width (`display_manager.cpp:103-110` fixes only x);
  - measure with LVGL sysmon, which is currently off (`lv_conf.h:1016`).
- Confidence: Verified structure; the gain is inferred.

**F-39: host tooling reports OTA success it can't know**
- The device restarts before sending SUCCESS (`ota_handler.cpp:221-249`).
- The host treats errors on END as success and always exits 0 (`tools/ble/grinder-ble.py:489-510`, `1235-1237`).
- The delta base is chosen by build number alone (`tools/build-scripts/pre_build.py`; `grinder-ble.py:348-350`), and build numbers are per-machine.
- Fix: identify the base by content hash; the host should reconnect and verify the build number.
- Confidence: Verified (per the review).

**F-40: exported data has no integrity check**
- The checksum stub always returns 0 (`grind_logging.cpp:848`).
- The file size is never sent.
- The 512-byte chunks ignore the negotiated MTU (`bluetooth.h:31`; pinned `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 256`).
- Also, the host deletes its database on every export, while the device keeps only 10 sessions (`grinder-ble.py:839-841`; `grind_logging.h:29`).
- Confidence: Verified stub; truncation is Uncertain.

### 4.4 Low

- **Docs that don't match the code:**

  | Item | Code | Docs |
  |---|---|---|
  | Maximum purge amount | 2.5 g (`grind_control.h`) | 5.0 g |
  | Auto-start window | 5 s + 1 s settle (`user.h`) | ~2 s |
  | Latency range | 30-300 ms | 30-200 ms |
  | Maximum pulse | latency + 250 ms | latency + 225 ms |
  | Update intervals (`system.h`) | 20 ms load cell / 16 ms UI | 25 / 50 ms (CLAUDE.md) |
  | NVS key names | `grinder_*` | `chute_*` (CLAUDE.md) |
  | Purge popup freshness rule | exists (`grind_controller.cpp:399-423`) | not documented |
  | "Hysteresis/persistence" in diagnostics | none | described |
  | Theme colour comments | 24-bit hex values | "RGB565" |
  | `THEME_COLOR_PRIMARY` | `0xFF3D00` | `0xFF0000` (CLAUDE.md) |
  | Motor Test / "+" pulse lengths | wrong (F-06) | 1 s / 100 ms |

- **Dead code:**
  - Unused queues: `ui_to_grind_queue`, `file_io_queue`.
  - Never called: `handle_grind_error()` (the task's only "emergency stop", `grind_control_task.cpp:286-301`), `TaskScheduler`, `performance_monitor`, the sampling-task recovery code.
  - Unused constants: `GRIND_PRIME_TARGET_WEIGHT_G`, `PREF_KEY_PRIME_ENABLED`.
  - Other: several never-emitted events, legacy logger functions, and hard-coded BLE "performance" JSON.
  - `BLE_REDUCED_CPU_FREQ_MHZ` equals the normal frequency, so the power-mode code does nothing.
- **Build:**
  - `lib_deps` is effectively unpinned (`lvgl/lvgl@ # ^9.3.0` leaves the version in a comment).
  - The platform comment says 55.03.34 but the URL is 55.03.32.
  - `pre_build.py` rewrites `git_info.h` (with a timestamp) on every build, and nearly everything includes it, so nearly everything rebuilds each time.
  - Misnamed display constants: `HW_DISPLAY_IPS_INVERT_*` and `COLOR_ORDER` are really panel offsets, and `ROTATION_DEG` is an index.
- **Flashing:** the web-flasher manifest writes the app to ota_0 (`0x320000`) and zeroes otadata. With a `factory` partition present, an older image left in factory could boot instead [TO CONFIRM].
- **Partitions:**
  - A full OTA is capped by the 2 MB `patch` partition, not the 3 MB slot.
  - The 3 MB `spiffs` partition holds at most ~0.74 MB of logs.
- **Smaller defects:**
  - Unaligned type-punned read when parsing START (`manager.cpp:683`).
  - The old-session cleanup sorts uninitialised memory (`grind_logging.cpp:1281-1323`).
  - The BLE task stack is 4 KB with large stack buffers on it.
  - `session_timestamp` is uptime, not Unix time.
  - `last_grind_ms` is written to NVS on every completion but never effectively used.
  - Diagnostics count grinding vibration toward "sustained noise".
  - The "5 min" weight-activity window only covers ~102 s of buffer.
  - The display filter's alpha and direction are inverted relative to its comment.
- **Host tools:**
  - A `@staticmethod` uses `self` (`grinder-ble.py:159-180`).
  - `grinder.py` may upload the `-mock` firmware.
  - `pip install` runs on every invocation.
  - Upload progress uses `\r`, so none is shown.
  - Paths with spaces break the custom target.

### 4.5 Checked and fine

- **HX711 read:** interrupts are masked for the whole bit-bang, and SCK stays well under the 60 µs power-down limit. Gain pulses are correct. The XOR offset-binary conversion is monotonic (`hx711_driver.cpp:141-160`).
- **LVGL threading:** LVGL runs only on the UI task. BLE status and grind events reach it through queues, and brightness changes also happen on the UI task.
- **Bounds:** pulse sizing can't divide by zero (flow is clamped), and `pulse_history` indexing stays in bounds.
- **OTA and export safety:** an interrupted OTA never touches the running partition, and `esp_ota_set_boot_partition()` validates the image. START and REQUEST_FILE are length-checked, and the session ID can't be used for path traversal.
- **Logger:** buffers are in PSRAM and bounds-checked, with no heap allocation on the 20 ms path.
- **Host/firmware protocol:** struct layouts and phase tables match between the two.

---

## 5. Grind algorithm improvements

These are listed after the safety fixes because they only make sense on top of them. Everything here is engineering judgement unless a line reference says otherwise.

1. **One command path, plus invariants.** All motor commands run on the control task. Enforce two invariants: "idle ⇒ motor off" and "motor on ⇒ a fresh sample within 300 ms". This removes F-01, F-15 and F-16 as a whole class.
2. **Better estimators:**
   - flow as a least-squares slope over the window (it is currently a two-point slope, `circular_buffer_math.cpp:368-375`);
   - "settled" as low noise and low slope and at least N samples;
   - tare over the full window.
3. **Timestamp samples at the DOUT falling edge** (interrupt plus `esp_timer_get_time()`). Samples are currently stamped when polled, 0-20 ms late, which is up to ~7-10% flow error on short windows (inference).
4. **Learn the coast ratio.** After each grind, compare the settled weight with the weight at the predictive stop (already recorded, `weight_grind_strategy.cpp:115`), and keep a bounded moving average in NVS instead of the fixed `GRIND_LATENCY_TO_COAST_RATIO 1.0`.
5. **Learn pulse gain within a session.** Size each pulse from the grams per millisecond measured on the previous pulses, not from the 95th-percentile continuous flow (`weight_grind_strategy.cpp:116`). Pulses start from standstill, so continuous flow is the wrong model.
6. **Use PRIME as a measurement run.** Take latency and flow from PRIME, skip PREDICTIVE when the remaining dose is smaller than the expected coast, and clamp the stop margin (one flow spike can currently stop the grind grams early).
7. **Predict the stop between samples.** At 10 SPS the stop decision can be up to 100 ms late, which is 0.1-0.3 g at 1-3 g/s (inference). Extrapolate from the last sample using the flow rate, and schedule the stop for the predicted crossing.
8. **Re-tare after purge, check the cup is present, and abort when no beans are delivered** (F-11, F-13).
9. **Scale the timeout with the target**, or cap the maximum target (F-37).

---

## 6. Touchscreen UI

### 6.1 What's there today

Everything below is from the code. The screen is 280×456 at about 326 ppi, roughly 12.8 px per mm and 22 mm wide.

**Ready screen** (`src/ui/screens/ready_screen.cpp`)
- Four swipe tabs: Single, Double, Custom, MENU. The tab bar is hidden and there is no page indicator.
- Each profile tab shows the profile name at 32 px and the target at 60 px (a cap height of about 3.3 mm, by inference). Long-press the target to edit it.

**The round button** (`src/ui/controllers/grinding_controller.cpp:25-36`)
- One 100 px circle, about 7.8 mm across, with a 24 px icon. It is the only primary control.
- It changes meaning with the state: GRIND, then STOP, then OK, and it becomes a gear on the MENU tab.

**Grinding screen**
- A 200 px arc with the live weight at 56 px.
- Tapping anywhere switches between the arc and chart layouts.
- Phases are not named on screen; only "TARE" is ever shown.

**Typical dose:** wake the screen, swipe to the profile, place the cup, press GRIND, deal with the purge prompt (first grind after boot, or after the freshness interval), wait, then OK. That is 3-7 touches.

### 6.2 UI bugs

Here U = UI finding and F = firmware finding from section 4.

**Checks:**
- I re-checked U1, U2, U7 and U10 against the code.
- LVGL behaviour was checked against the LVGL v9.3.0 source. `platformio.ini` doesn't actually pin the LVGL version, so which one gets compiled is [TO CONFIRM].

**U1: High. A swipe that starts on the round button starts a grind**
- **Where:** the button's parent is the screen, not the tabview (`grinding_controller.cpp:25-27`). It acts on `LV_EVENT_CLICKED` (`:59-69`) and calls `start_grind()` in READY (`:197-217`).
- **Why:** LVGL sends CLICKED on release whenever nothing scrolled, and a gesture does not cancel the click. PRESS_LOCK is on by default.
- **Result:** swiping across the lower screen to change profile, starting on the circle, starts the grinder. With Swipe enabled it also toggles the mode.
- **Fix:** ignore the click if the finger moved more than about 30 px or a gesture fired, or clear `LV_OBJ_FLAG_PRESS_LOCK` on this button.

**U2: High. Double-tapping STOP or OK starts a new grind**
- **Where:** the same button spot becomes PLAY about 16 ms after STOP or OK is handled (`grinding_controller.cpp:180-229`, `:294-326`, `:508-513`). There is no re-arm delay.
- **Result:**
  - STOP-STOP stops the grind, then re-tares with coffee in the cup and starts again.
  - OK-OK starts a dose, possibly with no cup.
- **Fix:** ignore further taps for 600-800 ms after the button changes meaning. STOP itself stays instant.

**U3 = F-02 and F-10:** OTA or export takes over the screen mid-grind and hides STOP.

**U4 = F-01, F-15, F-16:** UI commands race the control loop.

**U5 = F-19:** dropped state events. The UI never re-syncs with the controller phase (`grinding_controller.cpp:151-178`).

**U6: Medium. The layout toggle sits directly above STOP and writes flash mid-grind**
- The whole 280×364 area is a tap target (`grinding_screen_arc.cpp:7,12`), and STOP spans y 346-446.
- Each toggle does an immediate NVS write (`grinding_screen.cpp:47-50`), which stalls both cores (F-23).
- **Fix:** make the toggle a long-press or a corner icon, and save the choice when the grind ends.

**U7: Medium. The completion screen shows the live scale reading, not the result**
- Where: `grinding_controller.cpp:157-165` overwrites the final weight every 16 ms.
- Result: lift the cup and the dose disappears. `docs/DOC.md:332` says the final settled weight is shown.

**U8: Medium. Lifting the cup during final settling shows "Err: neg wt" instead of the result**
- Where: `grind_controller.cpp:551-567`, which is active in FINAL_SETTLING.
- There is also no on-screen "leave the cup" cue.

**U9: Medium. The Motor Test timer forces MENU 2 s later, from any screen**
- Where: `menu_controller.cpp:607-630`.
- Combined with `start_grind()` having no `is_active()` guard (`grind_controller.cpp:111`), this can hide STOP during a grind.

**U10: Medium. The first tap on a dimmed screen also presses whatever is under it**
- Where: `screen_timeout_controller.cpp:13-67` only changes brightness; every touch still reaches LVGL (`display_manager.cpp:154-166`).
- **Fix:** on a touch while dimmed, undim and call `lv_indev_wait_release()`.

**U11: Medium. Tare and calibration failures are silent, and a bad calibration is saved anyway**
- Where: `WeightSensor.cpp:250-252`, `305-306`; `blocking_overlay.cpp:126-136`.
- The UI freezes for about 4 s (tare) or about 12 s (calibration) behind a static "Please Wait".

**U12: Medium. GRIND silently does nothing when the load cell has a fault**
- Where: `grind_controller.cpp:115-118`.
- **Fix:** show "Scale fault - check wiring".

**U13: Medium. Hold-to-jog adds an extra step on release**
- Where: `edit_controller.cpp:95-124`, `calibration_controller.cpp:148-173`. A CLICKED event fires after the long press.
- **Fix:** use `LV_EVENT_SHORT_CLICKED` for the single step.

**U14: Low-Medium. Touch driver**
- A single failed I2C read mid-press becomes a release plus a click (`touch_driver.cpp:86-93`).
- The phantom-press risk is covered in F-32.
- **Fix:** debounce the release over 2-3 reads.

**Low-severity UI items**
- **Factory Reset:** the text says it clears grind history, but it only erases NVS. Session IDs restart at 1 and later overwrite old files (`menu_controller.cpp:97-103`, `564-581`; `grind_logging.cpp:67`, `1184-1188`).
- **Warning icon:** documented as tappable (`DOC.md:270`) but isn't (`status_indicator_controller.cpp:29-35`).
- **Purge-prompt checkbox:** its "2×" scaling has no effect, leaving it about 40 px tall (`purge_confirm_screen.cpp:59-66`).
- **Overlaps:** the auto-tune console runs under the Cancel button, and the calibration noise text overlaps the title (`autotune_screen.cpp:24-86`, `calibration_screen.cpp:42`, `129-130`).
- **Text bugs:**
  - "12.34g g" (`ui_helpers.cpp:53`)
  - "Last grind >0h ago" with a 0.5 h freshness setting (`ui_manager.cpp:257-261`)
  - "-0.0g" on the error screen
  - "what do do with the grinded coffee" (`menu_screen.cpp:382`)
- **Arc overflow:** weights of 100 g or more overlap the arc ring, and wider ones are clipped. There is about 176 px inside the ring; "100.0g" at 56 px is about 184 px wide (`grinding_screen_arc.cpp:36-51`).
- **Titles too wide:** "Grind Settings" (~263 px, in the menu header next to the back chevron), "FACTORY RESET" (~304 px) and "Reset Diagnostics" (~328 px) at 36 px on a 280 px screen. The dialog titles wrap, pushing text out of view (`confirm_screen.cpp:21-24`).
- **Status icons:** they sit over the close (✗) button in Edit and Calibration, and are restyled every frame.
- **Redundant updates:** chart `set_mode` runs and labels are re-set every cycle even when nothing changed (`grinding_controller.cpp:157-173`, `466`).
- **Overlay callbacks:** the blocking overlay clears its completion callback after invoking it, which is fragile if the callback starts another operation (`blocking_overlay.cpp:95-102`).
- **Error screen:** it auto-dismisses after 60 s (`grinding_controller.cpp:652-658`), though the docs say it requires acknowledgment.

### 6.3 Proposed redesign

The principles:
- one large primary action, in the same place, that never changes meaning under your finger;
- the result stays on screen;
- the screen always says what it's doing;
- no static bright elements left burning into the AMOLED.

The sketches are illustrative, not pixel-exact.

```
READY                          GRINDING                       DONE
+--------------------------+   +--------------------------+   +--------------------------+
| o * o o            BT  ! |   | DOUBLE  target 18.0 g    |   | DOUBLE                   |
|                          |   |                          |   |                          |
|         DOUBLE           |   |      ( progress arc )    |   |       18.02 g            |
|       18.0 g             |   |          12.4 g          |   |   +0.02 g   in tolerance |
|   [ -0.1 ]    [ +0.1 ]   |   |                          |   |   13.8 s    2 pulses     |
|  Last 18.02 g  13.8 s    |   |  Grinding...     6.2 s   |   |  (lift cup to finish)    |
|                          |   |                          |   |                          |
| +----------------------+ |   | +----------------------+ |   | +--------------+ +-----+ |
| |    GRIND  18.0 g     | |   | |        STOP          | |   | |     DONE     | |  +  | |
| +----------------------+ |   | +----------------------+ |   | +--------------+ +-----+ |
+--------------------------+   +--------------------------+   +--------------------------+
```

Prioritised proposals. Effort: S = hours, M = a day or two, L = more.

1. **Primary button → full-width bottom bar**
   - About 260×110 px (≈20×8.6 mm), labelled with an icon and a word.
   - STOP acts on press.
   - A re-arm lockout after each change of meaning, and cancel-on-drag. This fixes U1, U2 and U6.
   - The upper area shrinks from 80% to about 72% of the height.
   - Effort M; risk: layout reflow.
2. **A result that stays**
   - The final weight is the big number, with ±error coloured against tolerance, grind time and pulse count.
   - The Ready tab keeps a "Last" line. This fixes U7.
   - Effort M.
3. **Phase line**
   - "Taring - keep still", "Purging", "Grinding", "Topping up 2/10", "Settling - leave cup".
   - The phase text already exists; it's only logged (`grinding_controller.cpp:421-422`). This addresses U8.
   - Effort S.
4. **Quick dose nudge on Ready**
   - ±0.1 g tap zones that save automatically, alongside long-press to edit.
   - Time mode gets its own jog rates (it currently jogs up to ~20 s/s).
   - Effort M; risk: accidental edits, so highlight the change for about 2 s.
5. **AMOLED burn-in protection**
   - Today the screen only dims, never below 15% (`menu_controller.cpp:642-660`).
   - Go fully black after N minutes of idle, or show a small drifting last-dose readout, and shift the layout 2-4 px periodically.
   - Wake on touch (the first touch is consumed; U10) or on cup weight.
   - Effort M.
6. **Touch tuning for 326 ppi**
   - LVGL defaults are a 10 px (0.8 mm) scroll threshold and a 400 ms long press, not overridden here (`display_manager.cpp:89-91`).
   - Proposal: about 24-32 px and about 600 ms.
   - Effort S.
7. **Discoverability**
   - Dim page dots, shown only when the screen is awake.
   - A small "hold to edit" hint.
   - Make the whole MENU tab tappable, and return from the menu to the last profile rather than to the MENU tab.
   - Effort S.
8. **Destructive actions**
   - Hold-to-confirm for Factory Reset and Purge Logs.
   - At least 40 px between confirm and cancel (currently 10 px, `ui_helpers.cpp:97`).
   - Keep the previous calibration until the final confirm.
   - Effort S-M.
9. **Plain-language errors with a next step**
   - For example, "Scale not settling - check nothing touches the cup" instead of "Timeout:FINA".
   - A persistent badge on Ready until the error is acknowledged.
   - Effort S.
10. **Legibility at arm's length**
    - An 80-96 px digits-only font for the weight.
    - Less 24 px text on the grinding screens.
    - Four bundled Montserrat sizes (50/52/54/58) are compiled but unused, so they could be swapped out; the flash saving is [TO CONFIRM from the map file].
    - Effort M.
11. **One rule for confirm and cancel**
    - Confirm is on the left in Edit, Calibration and Confirm, but on the right in Auto-tune and the purge prompt.
    - Use the same side everywhere, and put a word next to each icon.
    - Effort S.
12. **Colour**
    - White on the time-mode blue (`0x00AAFF`) is 2.56:1, and white on the warning orange (`0xCC8800`) is 2.96:1. These are WCAG relative-luminance ratios I computed; the white icon on the blue GRIND button is hard to read.
    - The chart's red/green pairing is a common colour-vision confusion.
    - Red currently means both GRIND and STOP. Whether to keep red for GRIND is your call; my suggestion is to reserve red for STOP and errors.
    - Effort S.
13. **Time mode shows time**
    - Show a large countdown instead of grams.
    - Effort S.
14. **Settings controls**
    - LVGL sliders jump to the tap point, and the purge-amount step is about 0.35 mm of travel.
    - Use -/+ steppers and make whole rows toggle.
    - Effort M.
15. **Purge prompt**
    - Two labelled buttons, "Discarded - continue" and "Keep - continue", instead of the tiny checkbox plus icons.
    - Effort S.

The community fork reports work in the same areas (changelog 1.5.0 and 1.5.6-1.5.7):
- easier short swipes;
- better button legibility;
- screensavers and an optional panel-off stage;
- first wake touch consumed.

I have not reviewed that code.

---

## 7. Feature ideas

- **On-device update arming plus passkey pairing** (F-09). Cheap, and removes the biggest exposure.
- **Last-shot and history on the device:** the last N results with error, time and number of pulses; per-profile averages.
- **Named profiles or beans:** the name length constant already exists (`USER_PROFILE_NAME_MAX_LENGTH 8`, `user.h`).
- **Burr-service reminder** based on motor runtime, which `statistics_manager` already tracks.
- **Settings and calibration backup/restore over BLE.**
- **Incremental, verified export:** a CRC per session, sessions since a given ID, and a database upsert instead of wipe-and-reload.
- **Wi-Fi web UI and Home Assistant.** The community fork already has these. If you add them, default them off and reuse the same arming step for updates and remote start.

---

## 8. Tooling and development

- **Cloud builds.** This environment's network policy blocks `api.registry.platformio.org` and `api.registry.nm1.platformio.org`, so PlatformIO can't install packages here. To get compile-checked changes from these sessions, allow those hosts in the environment's network settings.
- **Pin exact library versions** in `platformio.ini`.
- **Add a CI build.** The existing `release.yml` builds only on tags.
- **Add host-side tests** of the controller around the mock scale, with fault injection: frozen sensor, NaN readings, STOP in every phase, dropped UI events. The mock currently has exact pulses and no faults (`src/hardware/mock_hx711_driver.cpp:300-316`), so it cannot catch F-03, F-04 or F-06.
- **A desktop LVGL simulator** would make UI iteration fast. The community fork has one (Windows).

---

## 9. Assumptions and items to confirm

**[TO CONFIRM]:**
- Whether GPIO 18's pad input is enabled at reset (decides F-14).
- GPIO 18's pull state during reset and bootloader, and whether the Eureka's transistor base has a pull-down (F-33, 3.2).
- That `-Ofast` folds `isnan()` on the target GCC 14.2 as it does on host GCC 13.3 (F-12).
- NimBLE host-task priority relative to the control task (priority 3) (F-24).
- Whether the FT3168 NACKs its address while idle, as the `CLAUDE.md` note implies (F-32).
- HX711, NAU7802, ADS1232 and ADS1220 datasheet figures, and whether your HX711 module has a RATE jumper and separate VCC/VDD (3.4, 3.5).
- The Waveshare board's regulator dropout (3.3 hold-up estimate).
- The PlatformIO USB upload offset (factory vs ota_0) (4.4, flashing).
- Web Bluetooth pairing behaviour with passkey (F-09).

**Assumptions:**
- Load current of 0.2-0.5 A in the hold-up estimate.
- An attack on F-09 needs physical proximity.
- The newer Waveshare board is what ships today. Upstream #144 and PR #145 (Aug 2026) show several users receiving it.

**Sources and revisions:**
- This fork: `afdacc8`.
- Community fork: `b4a0be6` (CHANGELOG top entry 1.5.9, 2026-09-06).
- Framework: arduino-esp32 3.3.2 with ESP-IDF 5.5.0 libs (pioarduino platform 55.03.32); `sdkconfig.h` from `framework-arduinoespressif32-libs/esp32s3/qio_opi`.
- ESP-IDF `release/v5.5` source: `rmt_tx.c`.
- Upstream issues #66, #128, #140, #141, #144, #154, #156, #157 and PR #145, all read 2026-09-27.

---

## 10. Proposed plan (for approval; nothing implemented yet)

Each phase would be committed separately, after you've checked it on hardware.

| Phase | Content | Hardware check you'd do |
|---|---|---|
| 0 | Base-codebase decision (section 2) | none |
| 1 | Motor safety: F-01 to F-05, F-10, F-13, and the pin-LOW-at-boot part of F-33 | STOP spam during purge; OTA attempt mid-grind (should be refused); unplug HX711 DOUT mid-grind; scope GPIO 18 at boot |
| 2 | Correctness: F-06 to F-08, F-11, F-12, F-14 to F-18, F-22, F-29 | logic analyser on pulse widths; purge amount persists; no false neg-wt; dose accuracy over 10 grinds |
| 3 | UI and UX (section 6) | hands-on |
| 4 | Security and OTA robustness: F-09, F-27, F-28; tooling F-39, F-40 | OTA with arming; forced bad image rolls back |
| 5 | Optional: algorithm improvements (section 5); ADC upgrade (3.4) | A/B dose accuracy and grind time |
