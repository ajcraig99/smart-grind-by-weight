/*
 * plant_params.h - the parameter list (single source of truth).
 *
 * One X-macro row per parameter:
 *   X(ID, "name", "unit", default, sweep_min, sweep_max, "source", "description")
 * `source` is either an exact "path:line" citation of upstream code or docs (verified by
 * tests/test_params.c when the repository root is supplied), or the literal "placeholder",
 * in which case the parameter must be listed in ASSUMPTIONS_PLANT.md with rationale and sweep
 * range (also checked by test_params).
 *
 * The enum below and the table in plant_params.c are both generated from this list, so they
 * cannot drift apart. PARAMS.md, params_default.json and params.schema.json are generated from
 * the table by tools/plant_paramgen (and compared in CTest).
 */
#ifndef PLANT_PARAMS_H
#define PLANT_PARAMS_H

/* clang-format off */
#define PLANT_PARAM_LIST(X) \
  /* ---- relay ---- */ \
  X(P_RELAY_ON_MS, "relay_on_latency_ms", "ms", 10.0, 2.0, 30.0, "placeholder", \
    "Delay from relay command energised to contacts closed.") \
  X(P_RELAY_OFF_MS, "relay_off_latency_ms", "ms", 8.0, 2.0, 30.0, "placeholder", \
    "Delay from relay command released to contacts open.") \
  /* ---- motor ---- */ \
  X(P_MOTOR_TAU_UP, "motor_tau_up_s", "s", 0.08, 0.03, 0.30, "placeholder", \
    "First-order spin-up time constant while the motor is powered.") \
  X(P_MOTOR_TAU_DOWN, "motor_tau_down_s", "s", 0.10, 0.05, 0.60, "placeholder", \
    "First-order coast-down time constant after power-off (burrs keep grinding while coasting).") \
  X(P_MOTOR_STALL_TAU, "motor_stall_tau_s", "s", 0.02, 0.005, 0.10, "placeholder", \
    "Speed decay time constant while the MOTOR_STALL fault is active.") \
  X(P_MOTOR_MIN_SPEED, "motor_min_speed", "1", 0.40, 0.10, 0.60, "placeholder", \
    "Normalised speed below which the burrs produce no grounds (tuned so the shortest productive pulse is about 42-46 ms, cf. src/config/debug.h:45).") \
  /* ---- grind path ---- */ \
  X(P_FLOW_NOMINAL, "flow_nominal_gps", "g/s", 1.9, 1.0, 3.0, "src/config/debug.h:37", \
    "Burr output at full speed with a full chamber: the firmware's own mock flow rate. Sweep range is the firmware's sane flow window, src/config/grind_control.h:78-79.") \
  X(P_GRIND_SETTING, "grind_setting_factor", "1", 1.0, 0.5, 1.5, "placeholder", \
    "Relative flow for the burr gap (coarser > 1, finer < 1); multiplies the nominal flow.") \
  X(P_BEAN_FACTOR, "bean_factor", "1", 1.0, 0.7, 1.3, "placeholder", \
    "Relative flow for bean hardness/roast; multiplies the nominal flow.") \
  X(P_FLOW_NOISE_FRAC, "flow_noise_frac", "1", 0.05, 0.0, 0.20, "placeholder", \
    "Standard deviation of the multiplicative flow noise (fraction of flow).") \
  X(P_FLOW_NOISE_TAU, "flow_noise_tau_s", "s", 0.15, 0.02, 1.0, "placeholder", \
    "Correlation time of the low-pass filtered flow noise.") \
  X(P_CLUMP_RATE, "clump_rate_hz", "1/s", 1.0, 0.0, 5.0, "placeholder", \
    "Poisson rate of clump releases at full speed and full chamber. Mean flow is kept at nominal.") \
  X(P_CLUMP_MASS, "clump_mass_g", "g", 0.05, 0.0, 0.30, "placeholder", \
    "Mean mass of a clump released at once (exponentially distributed).") \
  /* ---- burr chamber and hopper ---- */ \
  X(P_BURR_TAPER_MASS, "burr_taper_mass_g", "g", 3.0, 1.0, 6.0, "placeholder", \
    "Chamber mass below which flow tapers linearly towards zero.") \
  X(P_BURR_RESIDUAL, "burr_residual_g", "g", 0.3, 0.0, 1.0, "placeholder", \
    "Mass that stays in the burr chamber and is never ground (flow reaches zero here: run-dry).") \
  X(P_BURR_CAPACITY, "burr_capacity_g", "g", 25.0, 18.0, 40.0, "placeholder", \
    "Burr chamber capacity; a single 18 g dose fits entirely inside.") \
  X(P_FEED_RATE, "feed_rate_gps", "g/s", 30.0, 5.0, 100.0, "placeholder", \
    "Maximum rate at which beans fall from the hopper into free chamber space.") \
  /* ---- chute and transport ---- */ \
  X(P_CHUTE_RETENTION, "chute_retention_g", "g", 0.25, 0.0, 1.5, "placeholder", \
    "Static retention capacity of the chute: grounds up to this mass stay until pushed out.") \
  X(P_CHUTE_TAU, "chute_tau_s", "s", 0.06, 0.02, 0.50, "placeholder", \
    "Time constant with which grounds above the retention capacity drain out of the chute.") \
  X(P_TRANSPORT_DELAY, "transport_delay_s", "s", 0.32, 0.05, 0.60, "placeholder", \
    "Fixed fall/transport delay from chute exit to the cup. Hard cap 1.0 s (ring buffer of 1000 one-ms bins).") \
  /* ---- cup and load cell ---- */ \
  X(P_DROP_HEIGHT, "drop_height_m", "m", 0.10, 0.02, 0.20, "placeholder", \
    "Free-fall height of grounds from chute exit to the cup; sets the stream impact force flow*sqrt(2*g*h)/g.") \
  X(P_CUP_MASS, "cup_mass_g", "g", 100.0, 20.0, 300.0, "placeholder", \
    "Default empty cup mass used by PLACE_CUP when no value is given.") \
  X(P_LC_COUNTS_PER_G, "lc_counts_per_g", "counts/g", -7050.0, -7400.0, -6700.0, "src/config/user.h:52", \
    "HX711 counts per gram (negative: more mass gives a lower code). Default equals the firmware default calibration factor; the sweep (+-5 %) is a placeholder for sensitivity mismatch.") \
  X(P_LC_BASELINE, "lc_baseline_code", "counts", -1048576.0, -2000000.0, 0.0, "src/config/debug.h:39", \
    "Signed HX711 code of the empty platform. The mock's offset-binary 0x700000 equals signed 0x700000^0x800000 = -1048576. Sweep range is a placeholder.") \
  X(P_LC_GAIN_ERROR, "lc_gain_error", "1", 0.0, -0.05, 0.05, "placeholder", \
    "Fractional gain error between true grams and counts (true sensitivity = lc_counts_per_g*(1+error)).") \
  X(P_LC_IDLE_NOISE, "lc_idle_noise_counts", "counts", 60.0, 0.0, 200.0, "src/config/debug.h:40", \
    "Peak of the uniform per-conversion ADC noise when idle, as in the firmware mock.") \
  X(P_LC_VIB_NOISE, "lc_vibration_noise_counts", "counts", 400.0, 0.0, 1200.0, "src/config/debug.h:41", \
    "Peak per-conversion noise from motor vibration at full speed (scales with motor speed).") \
  X(P_LC_NAT_FREQ, "lc_natural_freq_hz", "Hz", 25.0, 5.0, 80.0, "placeholder", \
    "Natural frequency of the platform/load cell second-order response.") \
  X(P_LC_DAMPING, "lc_damping", "1", 0.30, 0.05, 1.0, "placeholder", \
    "Damping ratio of the platform/load cell response (lightly damped < 1).") \
  X(P_LC_CREEP_FRAC, "lc_creep_frac", "1", 0.0005, 0.0, 0.005, "placeholder", \
    "Fraction of a load change that arrives slowly (creep).") \
  X(P_LC_CREEP_TAU, "lc_creep_tau_s", "s", 30.0, 5.0, 300.0, "placeholder", \
    "Time constant of the creep fraction.") \
  X(P_LC_DRIFT_LIN, "lc_drift_g_per_min", "g/min", 0.002, -0.02, 0.02, "placeholder", \
    "Deterministic linear zero drift of the sensed mass.") \
  X(P_LC_DRIFT_WALK, "lc_drift_walk_g_per_rtmin", "g/sqrt(min)", 0.001, 0.0, 0.01, "placeholder", \
    "Random-walk zero drift intensity (standard deviation after 1 min is this value).") \
  X(P_LC_IMPACT_FRAC, "lc_place_impact_frac", "1", 0.5, 0.0, 3.0, "placeholder", \
    "Peak of the cup placement impact transient as a fraction of the placed mass.") \
  X(P_LC_IMPACT_TAU, "lc_impact_tau_s", "s", 0.03, 0.005, 0.10, "placeholder", \
    "Decay time constant of placement and bump transients.") \
  /* ---- HX711 ---- */ \
  X(P_HX711_SPS, "hx711_sps", "1/s", 10.0, 10.0, 80.0, "src/config/hardware.h:71", \
    "Output data rate: 10 (RATE pin low, the firmware setting) or 80 (RATE pin high, docs/TROUBLESHOOTING.md:113). Any value in 1..1000 is accepted and only changes the conversion period.") \
  X(P_HX711_SETTLE_10, "hx711_settle_ms_10sps", "ms", 400.0, 100.0, 600.0, "placeholder", \
    "Output settling time after power-up at 10 SPS (recollection of the HX711 datasheet, unverified offline).") \
  X(P_HX711_SETTLE_80, "hx711_settle_ms_80sps", "ms", 50.0, 10.0, 150.0, "placeholder", \
    "Output settling time after power-up at 80 SPS (recollection of the HX711 datasheet, unverified offline).")
/* clang-format on */

enum plant_param_id {
#define PLANT_X_ENUM(id, name, unit, def, mn, mx, src, desc) id,
    PLANT_PARAM_LIST(PLANT_X_ENUM)
#undef PLANT_X_ENUM
    PLANT_PARAM_COUNT
};

#endif /* PLANT_PARAMS_H */
