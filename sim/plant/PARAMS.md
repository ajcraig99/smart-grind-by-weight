# Plant parameters

Generated from `plant_params.h` by `plant_paramgen` (do not edit; CTest fails if this file is out of date).
`source` is an exact `path:line` citation of the upstream firmware repository (verified by `tests/test_params.c`), or
`placeholder`, in which case the rationale and sweep range are in `ASSUMPTIONS_PLANT.md`.

| Name | Unit | Default | Sweep min | Sweep max | Source | Description |
|------|------|---------|-----------|-----------|--------|-------------|
| `relay_on_latency_ms` | ms | 10 | 2 | 30 | placeholder | Delay from relay command energised to contacts closed. |
| `relay_off_latency_ms` | ms | 8 | 2 | 30 | placeholder | Delay from relay command released to contacts open. |
| `motor_tau_up_s` | s | 0.08 | 0.03 | 0.3 | placeholder | First-order spin-up time constant while the motor is powered. |
| `motor_tau_down_s` | s | 0.1 | 0.05 | 0.6 | placeholder | First-order coast-down time constant after power-off (burrs keep grinding while coasting). |
| `motor_stall_tau_s` | s | 0.02 | 0.005 | 0.1 | placeholder | Speed decay time constant while the MOTOR_STALL fault is active. |
| `motor_min_speed` | 1 | 0.4 | 0.1 | 0.6 | placeholder | Normalised speed below which the burrs produce no grounds (tuned so the shortest productive pulse is about 42-46 ms, cf. src/config/debug.h:45). |
| `flow_nominal_gps` | g/s | 1.9 | 1 | 3 | src/config/debug.h:37 | Burr output at full speed with a full chamber: the firmware's own mock flow rate. Sweep range is the firmware's sane flow window, src/config/grind_control.h:78-79. |
| `grind_setting_factor` | 1 | 1 | 0.5 | 1.5 | placeholder | Relative flow for the burr gap (coarser > 1, finer < 1); multiplies the nominal flow. |
| `bean_factor` | 1 | 1 | 0.7 | 1.3 | placeholder | Relative flow for bean hardness/roast; multiplies the nominal flow. |
| `flow_noise_frac` | 1 | 0.05 | 0 | 0.2 | placeholder | Standard deviation of the multiplicative flow noise (fraction of flow). |
| `flow_noise_tau_s` | s | 0.15 | 0.02 | 1 | placeholder | Correlation time of the low-pass filtered flow noise. |
| `clump_rate_hz` | 1/s | 1 | 0 | 5 | placeholder | Poisson rate of clump releases at full speed and full chamber. Mean flow is kept at nominal. |
| `clump_mass_g` | g | 0.05 | 0 | 0.3 | placeholder | Mean mass of a clump released at once (exponentially distributed). |
| `burr_taper_mass_g` | g | 3 | 1 | 6 | placeholder | Chamber mass below which flow tapers linearly towards zero. |
| `burr_residual_g` | g | 0.3 | 0 | 1 | placeholder | Mass that stays in the burr chamber and is never ground (flow reaches zero here: run-dry). |
| `burr_capacity_g` | g | 25 | 18 | 40 | placeholder | Burr chamber capacity; a single 18 g dose fits entirely inside. |
| `feed_rate_gps` | g/s | 30 | 5 | 100 | placeholder | Maximum rate at which beans fall from the hopper into free chamber space. |
| `chute_retention_g` | g | 0.25 | 0 | 1.5 | placeholder | Static retention capacity of the chute: grounds up to this mass stay until pushed out. |
| `chute_tau_s` | s | 0.06 | 0.02 | 0.5 | placeholder | Time constant with which grounds above the retention capacity drain out of the chute. |
| `transport_delay_s` | s | 0.32 | 0.05 | 0.6 | placeholder | Fixed fall/transport delay from chute exit to the cup. Hard cap 1.0 s (ring buffer of 1000 one-ms bins). |
| `drop_height_m` | m | 0.1 | 0.02 | 0.2 | placeholder | Free-fall height of grounds from chute exit to the cup; sets the stream impact force flow*sqrt(2*g*h)/g. |
| `cup_mass_g` | g | 100 | 20 | 300 | placeholder | Default empty cup mass used by PLACE_CUP when no value is given. |
| `lc_counts_per_g` | counts/g | -7050 | -7400 | -6700 | src/config/user.h:52 | HX711 counts per gram (negative: more mass gives a lower code). Default equals the firmware default calibration factor; the sweep (+-5 %) is a placeholder for sensitivity mismatch. |
| `lc_baseline_code` | counts | -1048576 | -2000000 | 0 | src/config/debug.h:39 | Signed HX711 code of the empty platform. The mock's offset-binary 0x700000 equals signed 0x700000^0x800000 = -1048576. Sweep range is a placeholder. |
| `lc_gain_error` | 1 | 0 | -0.05 | 0.05 | placeholder | Fractional gain error between true grams and counts (true sensitivity = lc_counts_per_g*(1+error)). |
| `lc_idle_noise_counts` | counts | 60 | 0 | 200 | src/config/debug.h:40 | Peak of the uniform per-conversion ADC noise when idle, as in the firmware mock. |
| `lc_vibration_noise_counts` | counts | 400 | 0 | 1200 | src/config/debug.h:41 | Peak per-conversion noise from motor vibration at full speed (scales with motor speed). |
| `lc_natural_freq_hz` | Hz | 25 | 5 | 80 | placeholder | Natural frequency of the platform/load cell second-order response. |
| `lc_damping` | 1 | 0.3 | 0.05 | 1 | placeholder | Damping ratio of the platform/load cell response (lightly damped < 1). |
| `lc_creep_frac` | 1 | 0.0005 | 0 | 0.005 | placeholder | Fraction of a load change that arrives slowly (creep). |
| `lc_creep_tau_s` | s | 30 | 5 | 300 | placeholder | Time constant of the creep fraction. |
| `lc_drift_g_per_min` | g/min | 0.002 | -0.02 | 0.02 | placeholder | Deterministic linear zero drift of the sensed mass. |
| `lc_drift_walk_g_per_rtmin` | g/sqrt(min) | 0.001 | 0 | 0.01 | placeholder | Random-walk zero drift intensity (standard deviation after 1 min is this value). |
| `lc_place_impact_frac` | 1 | 0.5 | 0 | 3 | placeholder | Peak of the cup placement impact transient as a fraction of the placed mass. |
| `lc_impact_tau_s` | s | 0.03 | 0.005 | 0.1 | placeholder | Decay time constant of placement and bump transients. |
| `hx711_sps` | 1/s | 10 | 10 | 80 | src/config/hardware.h:71 | Output data rate: 10 (RATE pin low, the firmware setting) or 80 (RATE pin high, docs/TROUBLESHOOTING.md:113). Any value in 1..1000 is accepted and only changes the conversion period. |
| `hx711_settle_ms_10sps` | ms | 400 | 100 | 600 | placeholder | Output settling time after power-up at 10 SPS (recollection of the HX711 datasheet, unverified offline). |
| `hx711_settle_ms_80sps` | ms | 50 | 10 | 150 | placeholder | Output settling time after power-up at 80 SPS (recollection of the HX711 datasheet, unverified offline). |
