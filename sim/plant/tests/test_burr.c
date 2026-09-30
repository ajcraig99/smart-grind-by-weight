#include "test_util.h"

/* Mass ground by a relay pulse of `ms` milliseconds from a full chamber, measured as the grounds
 * that have left the burr chamber after everything has drained. */
static double pulse_mass(int ms) {
    struct sim_plant* p = t_new(1);
    double m;
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 0, 1.0);
    t_steps(p, 1, 1, ms, SIM_PLANT_DT_S);
    t_run(p, 0, 2.0);
    m = t_ground_out(p);
    free(p);
    return m;
}

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    int i;

    /* ---- steady flow: exactly nominal with noise and clumps off ---- */
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 1, 2.0);
    o = t_out(p);
    CHECK_NEAR(o.flow_burr_gps, 1.9, 1e-3);
    CHECK_NEAR(o.motor_speed, 1.0, 1e-6);

    /* grind setting and bean factors multiply the flow */
    t_set(p, "grind_setting_factor", 0.8);
    t_set(p, "bean_factor", 1.1);
    t_run(p, 1, 0.2);
    CHECK_NEAR(t_out(p).flow_burr_gps, 1.9 * 0.8 * 1.1, 1e-3);
    t_set(p, "flow_nominal_gps", 2.5);
    t_run(p, 1, 0.2);
    CHECK_NEAR(t_out(p).flow_burr_gps, 2.5 * 0.8 * 1.1, 1e-3);

    /* ---- steady mean flow with default noise and clumps ---- */
    plant_init(p, 5);
    plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
    t_run(p, 1, 1.0);
    {
        double mass = 0.0, mx = 0.0;
        const int n = 8000;
        for (i = 0; i < n; i++) {
            t_run(p, 1, 0.001);
            o = t_out(p);
            mass += o.flow_burr_gps * SIM_PLANT_DT_S;
            if (o.flow_burr_gps > mx) mx = o.flow_burr_gps;
        }
        CHECK_NEAR(mass / 8.0, 1.9, 0.08); /* mean over 8 s within about 4 % */
        CHECK(mx > 10.0);                  /* clumps released at once show up as spikes */
    }

    /* no clumps, no noise: never above nominal */
    plant_init(p, 5);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
    for (i = 0; i < 4000; i++) {
        t_run(p, 1, 0.001);
        CHECK(t_out(p).flow_burr_gps <= 1.9 + 1e-9);
    }

    /* flow noise statistics: stationary std of the multiplier equals flow_noise_frac */
    plant_init(p, 11);
    t_quiet(p);
    t_set(p, "flow_noise_frac", 0.05);
    plant_action(p, SIM_ACT_LOAD_BEANS, 400.0);
    t_set(p, "burr_capacity_g", 400.0);
    t_run(p, 1, 1.0);
    {
        double s = 0.0, s2 = 0.0;
        const int n = 60000;
        for (i = 0; i < n; i++) {
            double x;
            t_run(p, 1, 0.001);
            x = t_out(p).flow_burr_gps / 1.9;
            s += x;
            s2 += x * x;
        }
        CHECK_NEAR(s / n, 1.0, 0.01);
        CHECK_NEAR(sqrt(s2 / n - (s / n) * (s / n)), 0.05, 0.012);
    }

    /* ---- hopper, chamber capacity, feeding ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
    t_run(p, 0, 0.3);
    o = t_out(p);
    CHECK_NEAR(o.m_burr_g, 9.0, 1e-6); /* 30 g/s for 0.3 s */
    t_run(p, 0, 2.0);
    o = t_out(p);
    CHECK_NEAR(o.m_burr_g, 25.0, 1e-9); /* chamber full */
    CHECK_NEAR(o.m_hopper_g, 15.0, 1e-9);
    CHECK_NEAR(o.m_loaded_g, 40.0, 1e-12);

    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 0, 0.5);
    CHECK(t_out(p).m_burr_g < 18.0);
    t_run(p, 0, 0.5);
    o = t_out(p);
    CHECK_NEAR(o.m_burr_g, 18.0, 1e-9); /* an 18 g dose sits entirely in the chamber */
    CHECK_NEAR(o.m_hopper_g, 0.0, 1e-9);

    /* feed block: beans stay in the hopper and nothing is ground */
    plant_init(p, 1);
    t_quiet(p);
    plant_fault(p, SIM_FAULT_FEED_BLOCK, 1, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 1, 3.0);
    o = t_out(p);
    CHECK_NEAR(o.m_hopper_g, 18.0, 1e-12);
    CHECK_NEAR(o.m_burr_g, 0.0, 1e-12);
    CHECK_NEAR(o.flow_burr_gps, 0.0, 1e-12);
    plant_fault(p, SIM_FAULT_FEED_BLOCK, 0, 0.0);
    t_run(p, 1, 1.0);
    o = t_out(p);
    CHECK(o.m_hopper_g < 1e-9);
    CHECK(o.flow_burr_gps > 0.5);
    /* a partly blocked chamber that was already loaded keeps grinding until run-dry */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 0, 1.0);
    plant_fault(p, SIM_FAULT_FEED_BLOCK, 1, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 10.0);
    t_run(p, 1, 40.0);
    o = t_out(p);
    CHECK_NEAR(o.m_hopper_g, 10.0, 1e-12); /* blocked beans never arrive */

    /* ---- run-dry: load 18.0 g, grind until the burrs are empty ---- */
    {
        const double loads[3] = {17.5, 18.0, 18.5};
        int k;
        for (k = 0; k < 3; k++) {
            plant_init(p, 100 + (uint64_t)k); /* default noise and clumps on */
            plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
            plant_action(p, SIM_ACT_LOAD_BEANS, loads[k]);
            t_run(p, 1, 40.0);
            o = t_out(p);
            CHECK_NEAR(o.m_burr_g, 0.3, 2e-3);        /* burr_residual_g */
            CHECK(o.flow_burr_gps < 1e-3);             /* flow has stopped */
            t_run(p, 0, 3.0);
            o = t_out(p);
            CHECK_NEAR(o.m_chute_g, 0.25, 1e-6);       /* chute_retention_g stays behind */
            CHECK_NEAR(o.m_cup_g, loads[k] - 0.3 - 0.25, 3e-3);
            CHECK_NEAR(o.m_inflight_g, 0.0, 1e-9);
            CHECK(fabs(o.conservation_error_g) < 1e-9);
        }
    }

    /* taper: flow is proportional to (m_burr - residual)/(taper - residual) below the taper mass */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 1, 0.5);
    {
        int crossed = 0;
        for (i = 0; i < 40000 && !crossed; i++) {
            t_run(p, 1, 0.001);
            o = t_out(p);
            if (o.m_burr_g < 1.65) {
                const double taper = (o.m_burr_g - 0.3) / (3.0 - 0.3);
                CHECK_NEAR(o.flow_burr_gps, 1.9 * taper * o.motor_speed, 0.03);
                crossed = 1;
            }
        }
        CHECK(crossed);
    }

    /* ---- minimum productive pulse, cf. the mock's hidden 42 ms (src/config/debug.h:45) ---- */
    {
        int d, threshold = -1;
        double prev = -1.0;
        for (d = 20; d <= 70; d++) {
            const double m = pulse_mass(d);
            if (threshold < 0 && m > 0.0) threshold = d;
            CHECK(m >= prev - 1e-12); /* monotone in pulse length */
            prev = m;
        }
        printf("minimum productive pulse: %d ms\n", threshold);
        CHECK(threshold >= 40 && threshold <= 50);
        CHECK(pulse_mass(30) == 0.0);
        CHECK(pulse_mass(36) == 0.0);
        CHECK(pulse_mass(60) > 0.02);
        CHECK(pulse_mass(120) > pulse_mass(60));
    }

    free(p);
    return t_finish("burr");
}
