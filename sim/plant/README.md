# sim/plant - physics model of the grinder, grounds path, load cell and HX711

Portable C11 (libc and libm only), no threads, no wall clock, no global mutable state, no malloc.
The state is one POD struct (`struct sim_plant`, about 9 kB, no pointers): `memcpy` saves and
restores a run and the copy continues byte for byte identically. All random draws come from a
seeded splitmix64 -> xoshiro256** generator (`rng.c`); `rand()` is never used. Double precision
throughout. The contract is `sim/sim_api.h` section 1; nothing outside `sim/plant/` is modified.

## Files

| File | Purpose |
|------|---------|
| `plant.h` | `struct sim_plant` layout, constants (ring size), extension action `SIM_ACT_WIPE_PLATFORM` |
| `plant.c` | init, step, outputs, actions, faults |
| `plant_params.h` | X-macro list: the single source of truth for parameters (name, unit, default, sweep, source, description) |
| `plant_params.c` | parameter table and `plant_param_*`, `plant_set_param`, `plant_get_param` |
| `rng.c`, `rng.h` | splitmix64 seeding, xoshiro256**, uniform, gaussian (Box-Muller), exponential |
| `params_default.json`, `params.schema.json`, `PARAMS.md` | generated from the table by `tests/plant_paramgen.c`; CTest compares them with the table |
| `ASSUMPTIONS_PLANT.md` | every placeholder with rationale and sweep range, plus structural assumptions |
| `tests/` | unit tests (CTest) |

Build and test (as in the acceptance command):

```
cmake -S . -B /tmp/claude-0/plant_build -DCMAKE_C_FLAGS="-m32 -msse2 -mfpmath=sse -Wall -Wextra"
cmake --build /tmp/claude-0/plant_build -j4
ctest --test-dir /tmp/claude-0/plant_build --output-on-failure
```

After editing `plant_params.h`: `cmake --build /tmp/claude-0/plant_build --target plant_regen_params`
rewrites the three generated files in this directory.

`add_subdirectory(sim/plant)` from another CMake project provides the static library target
`simplant` (PUBLIC include dirs: `sim/plant` and `sim`; links `m`); tests are built only when
this directory is the top-level project (or `-DSIMPLANT_BUILD_TESTS=ON`).

## Step order

`plant_step(p, in, dt)` advances in this order (all quantities below are per step):

1. latch inputs, `t += dt`
2. motor (uses the contact state from the start of the step), then relay
3. hopper -> burr chamber feed
4. burr output -> chute
5. chute drain -> transport ring
6. ring release -> cup or platform
7. load cell mechanics
8. HX711 conversion

## Equations

Symbols: `dt` step, `a(tau) = exp(-dt/tau)`, parameters in `monospace`.

**Relay.** `target` = last commanded level; a timer restarts whenever the command changes. The
internal contact state takes `target` once the timer reaches `relay_on_latency_ms` (closing) or
`relay_off_latency_ms` (opening). A command that flips back before the latency expires never moves
the contact. Faults override the output: `RELAY_STUCK_ON` -> 1 (wins over stuck-off), `RELAY_STUCK_OFF`
-> 0; the internal state keeps following the command underneath.

**Motor.** `powered = contact && !stall`. Speed `w` (0..1):
`w <- target + (w - target) * a(tau)` with `target = powered`, `tau = motor_tau_up_s` when powered,
`motor_tau_down_s` when not (coasting, still grinding), `motor_stall_tau_s` while the stall fault is
active (regardless of power). Grinding uses the mean speed over the step, `wm`:
`s = wm` if `wm >= motor_min_speed` else `0`.

**Hopper and burr chamber.** `feed = min(m_hopper, feed_rate_gps*dt, burr_capacity_g - m_burr)`
(zero while `FEED_BLOCK`). Flow noise `n` is a low-pass filtered gaussian with stationary standard
deviation `flow_noise_frac` and correlation time `flow_noise_tau_s`:
`n <- a*n + sqrt(1 - a^2) * flow_noise_frac * N(0,1)`; multiplier `1 + n` (clamped 0..4).
`taper = clamp((m_burr - r) / (T - r), 0, 1)` with `r = burr_residual_g`, `T = burr_taper_mass_g`.
Nominal flow `F = flow_nominal_gps * grind_setting_factor * bean_factor`. Regular output
`(F - clump_rate_hz*clump_mass_g)+ * s * taper * (1 + n) * dt`; with probability
`1 - exp(-clump_rate_hz * s * taper * dt)` a clump of mass `clump_mass_g * Exp(1)` is added in the same
step, so the mean flow equals `F`. The output is clamped to `m_burr - r` (the residual never grinds)
and moved exactly from the chamber to the chute. Run-dry: with the linear taper, `m_burr` approaches
`r` exponentially with time constant `(T - r) / F` (about 1.4 s at defaults) and the flow reaches zero only
asymptotically.

**Chute.** `excess = (m_chute - chute_retention_g)+`, `out = excess * (1 - a(chute_tau_s))`.
Grounds up to the retention stay until pushed out. `CLEAN_CHUTE` moves the whole store to `m_spilled`.

**Transport.** `out` is added to a ring buffer bin for time `t + transport_delay_s`. Bins are 1 ms
wide regardless of `dt`; `PLANT_RING_BINS = 1024`, so `transport_delay_s` is capped at 1.0 s
(1000 bins). Bins that have come due are released each step; `flow_cup_gps = released / dt`;
`m_inflight_g` is the sum of unreleased bins. Released grounds go to the cup if `cup_present`, otherwise
to `m_platform`.

**Cup.** `m_cup` is the content of the cup wherever it is. `PLACE_CUP` puts it on the platform; a
placement adds an impact `lc_place_impact_frac * (vessel + contents)` to the transient force.
`REMOVE_CUP` lifts it (contents go too). `EMPTY_CUP` moves the contents to `m_spilled` only while the
cup is off the platform (ignored otherwise: lift, empty, replace is the caller's job). `PRESS` sets a
steady extra mass (negative values are treated as 0). `BUMP` adds a transient of the given peak
grams. Extension: `SIM_ACT_WIPE_PLATFORM` (8) moves `m_platform` to `m_spilled`.

**Load cell.** Static load `u_s = (vessel + m_cup if cup_present) + m_platform + press`
(`scale_true_g`). Driving force in gram equivalents:
`u = (1 - f) u_s + f * LP(u_s; lc_creep_tau_s) + flow_cup * sqrt(2 g h) / g + impact`
with `f = lc_creep_frac`, `h = drop_height_m`, `g = 9.80665 m/s^2`, and `impact` decaying with
`lc_impact_tau_s`. The mechanical response is the lightly damped second-order system
`x'' + 2 z w x' + w^2 x = w^2 u`, `w = 2 pi lc_natural_freq_hz`, `z = lc_damping`, integrated
with symplectic Euler sub-steps of at most `0.05/w` seconds. Zero drift adds
`lc_drift_g_per_min/60 * t` plus a random walk with intensity `lc_drift_walk_g_per_rtmin`. Motor
vibration adds `speed * v * lc_vibration_noise_counts * burst / |lc_counts_per_g|` where `v` is a
uniform (-1,1) draw refreshed once per conversion period. `scale_signal_g = x + drift + vibration`.

**HX711.** Each conversion averages `scale_signal_g` over its period `1/hx711_sps`, then
`code = round(lc_baseline_code + avg * lc_counts_per_g * (1 + lc_gain_error) + U(-1,1) * lc_idle_noise_counts * burst)`
clamped to the signed 24-bit range. `lc_counts_per_g` is negative, so more mass gives a lower code
(placing 100 g lowers it by 705000). `lc_baseline_code = -1048576` is the firmware mock's offset-binary
`0x700000` as a signed code (`0x700000 ^ 0x800000`). On completion: `code` latched, `hx711_seq++`,
`hx711_ready = 1` (an unconsumed conversion is overwritten); `plant_hx711_consume` clears `ready`.
While `hx711_powered == 0` or the module is disconnected there are no conversions and `ready = 0`;
on the inactive-to-active transition the first conversion arrives after `hx711_settle_ms_10sps`
(`hx711_settle_ms_80sps` when the rate is 40 SPS or more). `hx711_sps` can be changed at run time
(clamped to 1..1000); it only changes the conversion period. Faults: `LC_DISCONNECT`
(`hx711_connected = 0`, `ready = 0`), `LC_STUCK` (conversions continue, code frozen at the last
value), `LC_NOISE_BURST` (idle and vibration noise multiplied by the value, default 20 when the value
is <= 0).

**Mass conservation.** `m_loaded = m_hopper + m_burr + m_chute + m_inflight + m_cup + m_platform + m_spilled`;
`conservation_error_g` is reported in the outputs. Every transfer moves the same computed value out of one
store and into the next. Worst error in the randomised test: below 1e-11 g.

## Tuning targets of the default parameters

| Quantity | Model (defaults, `tests/test_timing.c`) | Firmware mock (`src/config/debug.h`) |
|----------|-----------------------------------------|--------------------------------------|
| Shortest productive pulse | 44 ms | 42 ms (line 45) |
| Start command to 0.5 g/s at the cup | 0.41 s (chute primed), 0.57 s (chute empty) | 0.5 s (line 43) |
| Stop command to flow at the cup below 0.5 g/s / below 0.05 g/s | 0.48 s / 0.61 s | 0.4 s (line 44) |
| Grounds for a 100 ms pulse | 0.120 g | `1.9 * (100 - 42) / 1000 = 0.110 g` |

The stop latency is longer than the mock's 0.4 s (exponential tail); change `transport_delay_s`,
`motor_tau_down_s`, `chute_tau_s` to move it. The firmware purges before weight-mode grinding, so the
primed-chute start latency is the normal case.

## Known limitations

- Placeholders are not measurements (see `ASSUMPTIONS_PLANT.md`); HX711 settling times are unverified offline.
- No motor load dependence, no relay bounce, transport without spread, uniform (not gaussian) ADC noise.
- Tested for dt between 0.5 ms and 10 ms; the ring buffer has 1 ms resolution.
- `tests/test_determinism.c` checks byte-identical outputs for the same binary. Results across
  compilers/libm implementations (native versus WASM) are not guaranteed bit-identical because `exp`, `log`,
  `cos` are taken from the platform libm.
