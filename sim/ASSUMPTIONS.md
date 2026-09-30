# Assumptions and placeholders

Every plant-model default that is not cited from upstream code or docs is listed here with its
rationale and the sweep range used by the Monte Carlo harness. The authoritative per-parameter
table (name, unit, default, sweep range, source) is generated from `sim/plant/plant_params.c`
into `sim/plant/PARAMS.md`.

## Runtime and shim assumptions

| ID | Assumption | Rationale | Effect if wrong |
|----|------------|-----------|-----------------|
| R1 | Both ESP32 cores are serialised on one cooperative scheduler; firmware code takes zero virtual time. | Determinism; single-threaded WASM. | Races between cores are not reproduced. |
| R2 | FreeRTOS tick 1 ms. | Verified: CONFIG_FREERTOS_HZ 1000 in framework-arduinoespressif32-libs 3.3.2 esp32s3/qio_opi/include/sdkconfig.h:1178 (downloaded by the pio test). | - |
| R3 | Arduino loopTask priority 1 on core 1. | Verified: framework-arduinoespressif32 3.3.2 cores/esp32/main.cpp:113. | - |
| R4 | Panel byte order: resolved from source, no longer an assumption. LVGL 9.5 swaps RGB565 bytes before flush when LV_COLOR_16_SWAP is set (lvgl src/core/lv_refr.c:1433-1434; include/lv_conf.h:34 sets it for the device), and Arduino_GFX 1.6.7 `draw16bitBeRGBBitmap` sends the bytes unchanged. The virtual panel decodes that path as big-endian. | Verified against the library sources fetched from GitHub. | None expected. |
| R5 | FT3168 register 0x02 read returns [count, xh, xl, yh, yl]; the driver masks count with 0x0F and coordinates with 0x0F high nibbles. | Matches src/hardware/touch_driver.cpp:98-102. | None for the firmware; the shim encodes what the driver decodes. |

## Plant model placeholders (copied from sim/plant/ASSUMPTIONS_PLANT.md; that file is checked by the plant tests)

### A. Parameters (placeholders)

#### Relay and motor

| Parameter | Default | Sweep | Rationale |
|-----------|---------|-------|-----------|
| `relay_on_latency_ms` | 10 ms | 2 to 30 | Small electromechanical relay operate time is of the order of milliseconds to tens of milliseconds (general engineering knowledge, not a datasheet value). 10 ms and `motor_tau_up_s` together put the shortest productive pulse near 44 ms, matching the mock's hidden 42 ms (`src/config/debug.h:45`). |
| `relay_off_latency_ms` | 8 ms | 2 to 30 | Same reasoning; release time chosen slightly shorter than operate time. |
| `motor_tau_up_s` | 0.08 s | 0.03 to 0.30 | Spin-up of a small grinder motor to full speed in a few tenths of a second; first-order is a simplification. Tuned with `motor_min_speed` and `relay_on_latency_ms` for the 42-46 ms minimum pulse. |
| `motor_tau_down_s` | 0.10 s | 0.05 to 0.60 | Coast-down of burrs and rotor after power-off; chosen so that grounds keep falling for about 0.1 s after the contact opens and the stop latency lands at 0.4-0.5 s. |
| `motor_stall_tau_s` | 0.02 s | 0.005 to 0.10 | Brief says "speed decays to 0 fast (tau about 20 ms)". |
| `motor_min_speed` | 0.40 | 0.10 to 0.60 | Fraction of full speed below which burrs do not cut beans. Purely a tuning knob for the minimum productive pulse (`-ln(1 - 0.4) * 0.08 s = 41 ms` of powered time). |

#### Grind path

| Parameter | Default | Sweep | Rationale |
|-----------|---------|-------|-----------|
| `grind_setting_factor` | 1.0 | 0.5 to 1.5 | Relative flow for burr gap; 1.0 = the nominal flow. The firmware's sane flow window (1.0 to 3.0 g/s, `src/config/grind_control.h:78-79`) around 1.9 g/s corresponds to factors 0.53 to 1.58, hence the sweep. |
| `bean_factor` | 1.0 | 0.7 to 1.3 | Relative flow for bean hardness and roast; a plausible spread, not measured. |
| `flow_noise_frac` | 0.05 | 0 to 0.20 | 5 % standard deviation of multiplicative flow noise; placeholder. |
| `flow_noise_tau_s` | 0.15 s | 0.02 to 1.0 | Correlation time of flow noise; placeholder. |
| `clump_rate_hz` | 1.0 1/s | 0 to 5 | Poisson clump rate at full speed; placeholder. The regular flow is reduced by `clump_rate_hz * clump_mass_g` so the mean flow stays nominal. |
| `clump_mass_g` | 0.05 g | 0 to 0.30 | Mean clump mass, exponentially distributed; placeholder. |
| `burr_taper_mass_g` | 3.0 g | 1 to 6 | Chamber mass below which flow falls linearly; placeholder. Sets the run-dry tail length `(taper - residual) / flow`, about 1.4 s at defaults. |
| `burr_residual_g` | 0.3 g | 0 to 1.0 | Mass that never grinds. Placeholder; real retention in the burr chamber of the user's grinder is not known. |
| `burr_capacity_g` | 25 g | 18 to 40 | Only has to exceed an 18 g single dose (brief: user single doses 18 g). Sweep lower bound equals the dose. |
| `feed_rate_gps` | 30 g/s | 5 to 100 | Gravity feed from hopper into free chamber space; fast so that a dose is in the chamber within about 0.6 s; placeholder. |

#### Chute and transport

| Parameter | Default | Sweep | Rationale |
|-----------|---------|-------|-----------|
| `chute_retention_g` | 0.25 g | 0 to 1.5 | Static retention in the chute. Placeholder; the brief asks for chute retention but gives no value. Also sets the gap between the empty-chute and primed-chute start latency (0.565 s versus 0.411 s at defaults). |
| `chute_tau_s` | 0.06 s | 0.02 to 0.50 | Drain time constant of the chute excess; placeholder tuned with `transport_delay_s` for the stop latency. |
| `transport_delay_s` | 0.32 s | 0.05 to 0.60 | Brief suggests 0.2-0.3 s; 0.32 s was chosen so that the primed-chute start latency reaches 0.41 s and the empty-chute one stays below 0.6 s (target 0.4-0.6 s, mock 0.5 s `debug.h:43`). Hard cap 1.0 s (ring buffer). |

#### Cup and load cell

| Parameter | Default | Sweep | Rationale |
|-----------|---------|-------|-----------|
| `drop_height_m` | 0.10 m | 0.02 to 0.20 | Chute exit to cup; placeholder. Stream impact force is `flow * sqrt(2 g h) / g`, about 0.27 g at 1.9 g/s. |
| `cup_mass_g` | 100 g | 20 to 300 | Default empty cup; placeholder. Not derived from the user's vessel. |
| `lc_gain_error` | 0 | -0.05 to 0.05 | Default none; sweep +-5 % covers a miscalibrated or drifting sensitivity. The firmware calibrates its own factor, so a nonzero true gain error tests that path. |
| `lc_natural_freq_hz` | 25 Hz | 5 to 80 | Platform plus load cell second-order response; placeholder. With the HX711 at 10 SPS the ringing is largely averaged out, at 80 SPS it is visible. |
| `lc_damping` | 0.30 | 0.05 to 1.0 | Lightly damped (overshoot 37 % of a step); placeholder. |
| `lc_creep_frac` | 0.0005 | 0 to 0.005 | 0.05 % of a load step arrives slowly; placeholder. |
| `lc_creep_tau_s` | 30 s | 5 to 300 | Creep time constant; placeholder. |
| `lc_drift_g_per_min` | 0.002 g/min | -0.02 to 0.02 | Deterministic thermal/zero drift; placeholder. |
| `lc_drift_walk_g_per_rtmin` | 0.001 g/sqrt(min) | 0 to 0.01 | Random-walk zero drift; placeholder (extra to the brief's list). |
| `lc_place_impact_frac` | 0.5 | 0 to 3 | Peak of the cup-placement impact as a fraction of the placed mass; placeholder (extra to the brief's list). |
| `lc_impact_tau_s` | 0.03 s | 0.005 to 0.10 | Decay of placement and bump transients; placeholder (extra to the brief's list). |

### HX711

| Parameter | Default | Sweep | Rationale |
|-----------|---------|-------|-----------|
| `hx711_settle_ms_10sps` | 400 ms | 100 to 600 | **Unverified offline.** Recollection of the HX711 datasheet output settling time at 10 SPS. The firmware waits 2 sample intervals plus a timeout of 2 intervals + 200 ms after power-up (`src/hardware/hx711_driver.cpp:42-47`), so a settling time well above about 600 ms would make `begin()` time out (inference from the delay and timeout shown there) and is outside the sweep. |
| `hx711_settle_ms_80sps` | 50 ms | 10 to 150 | **Unverified offline.** Recollection of the HX711 datasheet settling time at 80 SPS. |

### B. Structural assumptions (not parameters)

| ID | Assumption | Effect if wrong |
|----|------------|-----------------|
| M1 | Relay contact follows the command after a fixed latency; a command that changes back before the latency expires is ignored (no contact bounce, no arc). | Very short glitches on the relay pin would behave differently on hardware. |
| M2 | Motor speed is first order in both directions; no load dependence (beans do not slow the motor). | Real spin-up depends on load. |
| M3 | Above `motor_min_speed` the burr output is proportional to speed; below it is exactly zero (a step, not a ramp). | Pulse mass for the shortest pulses is sensitive to this. |
| M4 | Beans fall from the hopper into the chamber regardless of the motor (gravity), limited by `feed_rate_gps` and free chamber space. | Bridging is only modelled through the FEED_BLOCK fault. |
| M5 | The flow taper is linear in chamber mass between `burr_residual_g` and `burr_taper_mass_g`, so run-dry is an exponential approach (flow reaches zero only asymptotically; the chamber never drops below the residual). | The real tail shape is unknown. |
| M6 | Chute: the first `chute_retention_g` grams stay until pushed out; the excess drains first order. The retention is never released by itself (only CLEAN_CHUTE removes it). | Retention in real chutes varies with humidity and static charge. |
| M7 | Transport is a pure delay (no spread). Flow spreading is provided by the chute drain only. | Flow step edges at the cup are sharper than reality beyond the chute filter. |
| M8 | Grounds that land on the bare platform stay there (m_platform) and weigh on the scale, also after a cup is placed on top; only the extension action WIPE_PLATFORM (8) clears them. | Scenarios that grind without a cup leave a permanent offset unless wiped. |
| M9 | PLACE_CUP with value <= 0 on a cup that was lifted earlier returns the same cup (same vessel mass and contents); only a first-ever placement uses `cup_mass_g`. PLACE_CUP on a cup already on the platform does nothing. | Differs from a literal "default mass every time" reading of `sim_api.h`; see interface notes. |
| M10 | HX711 noise is uniform +-peak per conversion (as in the firmware mock `src/hardware/mock_hx711_driver.cpp:268-279`), independent of the conversion rate. Real ADC noise is usually higher at 80 SPS. | 80 SPS noise is underestimated. |
| M11 | Motor vibration is one uniform draw per conversion period (constant over the period) scaled by motor speed, so that after the conversion averaging it reaches the full +-400 counts cited from the mock instead of being averaged down. | The real spectrum of vibration and its aliasing into 10 SPS is not modelled. |
| M12 | The HX711 is powered and settled at plant init. The settling time applies on every inactive-to-active transition (SCK power-down released, or module reconnected). | A plant created at t = 0 has its first conversion 1/SPS later, not after the settling time. |
| M13 | The HX711 output code is sign-consistent with the firmware: signal mass times `lc_counts_per_g` (negative) added to `lc_baseline_code`; the firmware sees offset-binary `code ^ 0x800000`. | None (verified against `src/hardware/hx711_driver.cpp:15-18`). |
| M14 | Step size: tested at 0.5, 1 and 4 ms (and 10 ms for conservation). The transport ring uses fixed 1 ms bins, so the delay resolution is 1 ms regardless of dt. | Steps larger than about 10 ms are not validated. |

## Runtime assumptions added after Tier 1

| ID | Assumption | Rationale | Effect if wrong |
|----|------------|-----------|-----------------|
| R6 | An undriven relay pin with only its internal pull-up reads as energised (`relay_pin_level`). | Conservative: the relay driver circuit is not modelled; a weak pull-up could turn a driver transistor on. Only occurs if firmware leaves the pin undriven. | Relay would stay off instead. |
| R7 | FT3168 reports 0 touches when not pressed (no NACK). | The firmware tolerates both (touch_driver.cpp). | Release is seen one poll earlier than with a NACKing controller. |
| R8 | Task watchdog models subscribed tasks only, not idle-task starvation. | Cooperative scheduler has no CPU contention. | A CPU hog would not trigger a watchdog reset in the twin. |
| R9 | Heap and flash sizes reported by ESP.* and heap_caps are fixed plausible values. | The twin has no memory model. | Diagnostics screens show made-up capacities. |
| R10 | Scripted user timings: 1.2 s reaction, 0.1 s taps, 0.8 s to lift/empty/replace the cup, OK tapped 3 s after the result. | Plausible human pace; sweepable per scenario (reaction_s). | Operator-timing-sensitive outcomes (purge prompt pause) shift slightly. |
