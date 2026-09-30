/*
 * Mass conservation over long randomised runs with actions, faults and parameter changes:
 *   m_loaded = hopper + burr + chute + in flight + cup + platform + spilled
 * (error below 1e-9 g; every store non-negative and finite).
 */
#include "test_util.h"

static double worst_err = 0.0;

static void check_state(const struct sim_plant* p, const char* what, long step) {
    const sim_plant_outputs_t o = t_out(p);
    const double e = fabs(o.conservation_error_g);
    const double stores[7] = {o.m_hopper_g, o.m_burr_g, o.m_chute_g, o.m_inflight_g,
                              o.m_cup_g, o.m_platform_g, o.m_spilled_g};
    int k;
    if (e > worst_err) worst_err = e;
    if (!(e < 1e-9)) {
        printf("FAIL: conservation error %.3g g (%s, step %ld)\n", e, what, step);
        t_fail++;
    }
    for (k = 0; k < 7; k++) {
        if (!(stores[k] >= -1e-12) || !isfinite(stores[k])) {
            printf("FAIL: store %d = %.9g (%s, step %ld)\n", k, stores[k], what, step);
            t_fail++;
        }
    }
    if (!isfinite(o.scale_signal_g) || !isfinite(o.motor_speed) || !isfinite(o.flow_cup_gps)) {
        printf("FAIL: non-finite output (%s, step %ld)\n", what, step);
        t_fail++;
    }
    t_checks++;
}

static double urand(plant_rng_t* r, double lo, double hi) {
    return lo + (hi - lo) * plant_rng_uniform(r);
}

static void random_event(struct sim_plant* p, plant_rng_t* r) {
    const int kind = (int)(plant_rng_next_u64(r) % 14u);
    switch (kind) {
    case 0: plant_action(p, SIM_ACT_LOAD_BEANS, urand(r, 0.0, 30.0)); break;
    case 1: plant_action(p, SIM_ACT_PLACE_CUP, (plant_rng_next_u64(r) & 1u) ? 0.0 : urand(r, 30.0, 250.0)); break;
    case 2: plant_action(p, SIM_ACT_REMOVE_CUP, 0.0); break;
    case 3: plant_action(p, SIM_ACT_EMPTY_CUP, 0.0); break;
    case 4: plant_action(p, SIM_ACT_BUMP, urand(r, -20.0, 80.0)); break;
    case 5: plant_action(p, SIM_ACT_PRESS, (plant_rng_next_u64(r) & 1u) ? 0.0 : urand(r, 0.0, 300.0)); break;
    case 6: plant_action(p, SIM_ACT_CLEAN_CHUTE, 0.0); break;
    case 7: plant_action(p, SIM_ACT_WIPE_PLATFORM, 0.0); break;
    case 8: plant_fault(p, SIM_FAULT_FEED_BLOCK, (int)(plant_rng_next_u64(r) & 1u), 0.0); break;
    case 9: plant_fault(p, SIM_FAULT_MOTOR_STALL, (int)(plant_rng_next_u64(r) & 1u), 0.0); break;
    case 10: plant_fault(p, SIM_FAULT_RELAY_STUCK_ON + (int)(plant_rng_next_u64(r) & 1u), (int)(plant_rng_next_u64(r) & 1u), 0.0); break;
    case 11: plant_fault(p, SIM_FAULT_LC_DISCONNECT + (int)(plant_rng_next_u64(r) % 3u), (int)(plant_rng_next_u64(r) & 1u), urand(r, 0.0, 30.0)); break;
    case 12: { /* randomise the grounds path parameters, including extremes of the sweep ranges */
        const char* names[8] = {"transport_delay_s", "chute_retention_g", "chute_tau_s", "burr_residual_g",
                                "burr_taper_mass_g", "flow_nominal_gps", "clump_mass_g", "feed_rate_gps"};
        const int k = (int)(plant_rng_next_u64(r) % 8u);
        const sim_param_info_t* info = plant_param_info(plant_param_index(names[k]));
        plant_set_param(p, names[k], urand(r, info->sweep_min, info->sweep_max));
        break;
    }
    default: plant_set_param(p, "hx711_sps", (plant_rng_next_u64(r) & 1u) ? 80.0 : 10.0); break;
    }
}

static void run_random(uint64_t seed, double dt, double seconds) {
    struct sim_plant* p = t_new(seed);
    plant_rng_t r;
    sim_plant_inputs_t in;
    const long n = (long)(seconds / dt + 0.5);
    long i, next_event = 0, next_relay = 0;
    int relay = 0;
    plant_rng_seed(&r, seed ^ UINT64_C(0xABCDEF));
    in.relay_cmd = 0;
    in.hx711_powered = 1;
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    for (i = 0; i < n; i++) {
        if (i >= next_event) {
            random_event(p, &r);
            next_event = i + (long)(urand(&r, 0.05, 2.0) / dt);
        }
        if (i >= next_relay) {
            relay = !relay;
            next_relay = i + (long)(urand(&r, 0.02, relay ? 5.0 : 3.0) / dt);
        }
        in.relay_cmd = relay;
        in.hx711_powered = ((plant_rng_next_u64(&r) % 4000u) != 0u) ? 1 : 0;
        plant_step(p, &in, dt);
        if (i % 7 == 0) check_state(p, "random run", i);
        if (t_out(p).hx711_ready && (i % 3 == 0)) plant_hx711_consume(p);
    }
    check_state(p, "end of random run", n);
    free(p);
}

int main(void) {
    uint64_t seed;
    /* 3 x 300 s at dt = 1 ms (900 000 steps), plus other step sizes */
    for (seed = 1; seed <= 3; seed++) run_random(seed, 0.001, 300.0);
    run_random(11, 0.0005, 100.0);
    run_random(12, 0.004, 400.0);
    run_random(13, 0.01, 600.0);
    printf("worst conservation error: %.3g g\n", worst_err);
    CHECK(worst_err < 1e-9);

    /* deterministic accounting check: 18 g in, nothing left behind anywhere else */
    {
        struct sim_plant* p = t_new(1);
        sim_plant_outputs_t o;
        plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
        plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
        t_run(p, 1, 40.0);
        t_run(p, 0, 3.0);
        o = t_out(p);
        CHECK_NEAR(o.m_hopper_g + o.m_burr_g + o.m_chute_g + o.m_inflight_g + o.m_cup_g + o.m_platform_g + o.m_spilled_g, 18.0, 1e-9);
        free(p);
    }
    return t_finish("conservation");
}
