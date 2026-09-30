#include "test_util.h"

/* Step until hx711_seq changes; returns the number of 1 ms steps taken, or -1. */
static int steps_to_next_conversion(struct sim_plant* p, int powered, int max_steps) {
    const uint32_t seq = t_out(p).hx711_seq;
    int i;
    for (i = 1; i <= max_steps; i++) {
        t_steps(p, 0, powered, 1, SIM_PLANT_DT_S);
        if (t_out(p).hx711_seq != seq) return i;
    }
    return -1;
}

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    int i;

    /* ---- defaults ---- */
    o = t_out(p);
    CHECK(o.hx711_connected == 1);
    CHECK(o.hx711_powered == 1);
    CHECK(o.hx711_ready == 0);
    CHECK_NEAR(o.hx711_sps, 10.0, 0.0);
    CHECK(o.hx711_code == -1048576); /* 0x700000 ^ 0x800000 as signed 24 bit */

    /* ---- cadence at 10 SPS: first conversion after 100 ms, then every 100 ms ---- */
    CHECK(steps_to_next_conversion(p, 1, 500) == 100);
    CHECK(t_out(p).hx711_ready == 1);
    for (i = 0; i < 20; i++) CHECK(steps_to_next_conversion(p, 1, 500) == 100);
    plant_init(p, 1);
    t_run(p, 0, 10.0);
    CHECK(t_out(p).hx711_seq == 100);

    /* ---- cadence at 80 SPS ---- */
    plant_init(p, 1);
    t_set(p, "hx711_sps", 80.0);
    t_run(p, 0, 10.0);
    CHECK_RANGE(t_out(p).hx711_seq, 799, 801);
    CHECK_NEAR(t_out(p).hx711_sps, 80.0, 0.0);

    /* changing the rate at run time only changes the period */
    plant_init(p, 1);
    t_run(p, 0, 2.0);
    CHECK(t_out(p).hx711_seq == 20);
    t_set(p, "hx711_sps", 80.0);
    t_run(p, 0, 2.0);
    CHECK_RANGE(t_out(p).hx711_seq, 20 + 159, 20 + 161);
    t_set(p, "hx711_sps", 10.0);
    t_run(p, 0, 2.0);
    CHECK_RANGE(t_out(p).hx711_seq, 20 + 160 + 19, 20 + 161 + 21);
    CHECK(plant_set_param(p, "hx711_sps", 1e9) == 0); /* clamped, not rejected */
    {
        int ok = 0;
        CHECK_NEAR(plant_get_param(p, "hx711_sps", &ok), 1000.0, 0.0);
        CHECK(ok == 1);
    }

    /* ---- ready / consume semantics ---- */
    plant_init(p, 1);
    t_run(p, 0, 0.099);
    CHECK(t_out(p).hx711_ready == 0);
    t_run(p, 0, 0.001);
    o = t_out(p);
    CHECK(o.hx711_ready == 1);
    CHECK(o.hx711_seq == 1);
    plant_hx711_consume(p);
    o = t_out(p);
    CHECK(o.hx711_ready == 0);
    CHECK(o.hx711_seq == 1); /* consuming does not change the sequence */
    t_run(p, 0, 0.099);
    CHECK(t_out(p).hx711_ready == 0);
    t_run(p, 0, 0.001);
    CHECK(t_out(p).hx711_ready == 1);
    CHECK(t_out(p).hx711_seq == 2);
    /* a new conversion overwrites an unconsumed one */
    t_run(p, 0, 1.0);
    o = t_out(p);
    CHECK(o.hx711_ready == 1);
    CHECK(o.hx711_seq == 12);

    /* ---- power-down and settling at 10 SPS ---- */
    plant_init(p, 1);
    t_run(p, 0, 0.5);
    CHECK(t_out(p).hx711_ready == 1);
    t_steps(p, 0, 0, 1, SIM_PLANT_DT_S);
    o = t_out(p);
    CHECK(o.hx711_powered == 0);
    CHECK(o.hx711_ready == 0);
    {
        const uint32_t seq = o.hx711_seq;
        t_steps(p, 0, 0, 3000, SIM_PLANT_DT_S);
        CHECK(t_out(p).hx711_seq == seq);
        CHECK(t_out(p).hx711_ready == 0);
    }
    /* power up: first conversion after the settling time (400 ms), then the normal cadence */
    CHECK(steps_to_next_conversion(p, 1, 2000) == 400);
    CHECK(steps_to_next_conversion(p, 1, 2000) == 100);
    /* power-up at 80 SPS settles in 50 ms */
    t_set(p, "hx711_sps", 80.0);
    t_steps(p, 0, 0, 10, SIM_PLANT_DT_S);
    CHECK(steps_to_next_conversion(p, 1, 2000) == 50);
    CHECK_RANGE(steps_to_next_conversion(p, 1, 2000), 12, 13);

    /* ---- disconnect ---- */
    plant_init(p, 1);
    t_run(p, 0, 0.5);
    CHECK(t_out(p).hx711_ready == 1);
    plant_fault(p, SIM_FAULT_LC_DISCONNECT, 1, 0.0);
    o = t_out(p);
    CHECK(o.hx711_connected == 0);
    CHECK(o.hx711_ready == 0);
    {
        const uint32_t seq = o.hx711_seq;
        t_run(p, 0, 3.0);
        o = t_out(p);
        CHECK(o.hx711_ready == 0);
        CHECK(o.hx711_seq == seq);
        CHECK(o.hx711_connected == 0);
    }
    plant_fault(p, SIM_FAULT_LC_DISCONNECT, 0, 0.0);
    CHECK(t_out(p).hx711_connected == 1);
    CHECK(steps_to_next_conversion(p, 1, 2000) == 400); /* re-plugged module settles like a power-up */

    /* ---- stuck: conversions keep coming, the code is frozen ---- */
    plant_init(p, 3);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    t_run(p, 0, 2.0);
    {
        const int32_t before = t_out(p).hx711_code;
        uint32_t seq;
        CHECK(before < -1048576 - 600000);
        plant_fault(p, SIM_FAULT_LC_STUCK, 1, 0.0);
        plant_action(p, SIM_ACT_PRESS, 500.0); /* the load changes a lot */
        seq = t_out(p).hx711_seq;
        for (i = 0; i < 30; i++) {
            t_run(p, 0, 0.1);
            CHECK(t_out(p).hx711_code == before);
        }
        CHECK(t_out(p).hx711_seq == seq + 30);
        CHECK(t_out(p).hx711_ready == 1);
        plant_fault(p, SIM_FAULT_LC_STUCK, 0, 0.0);
        t_run(p, 0, 1.0);
        CHECK(t_out(p).hx711_code < before - 3000000); /* 500 g more: about -3.5e6 counts */
    }

    /* ---- noise burst: idle noise peak 60 counts, multiplied by the fault value ---- */
    {
        const double mults[3] = {20.0, 5.0, 0.0}; /* 0 or less selects the default 20 */
        const double peaks[3] = {1200.0, 300.0, 1200.0};
        int k;
        for (k = 0; k < 4; k++) {
            double mx = 0.0;
            plant_init(p, 9);
            t_quiet(p);
            t_set(p, "lc_idle_noise_counts", 60.0);
            if (k < 3) plant_fault(p, SIM_FAULT_LC_NOISE_BURST, 1, mults[k]);
            t_run(p, 0, 0.2);
            for (i = 0; i < 600; i++) {
                double d;
                t_run(p, 0, 0.1);
                d = fabs((double)t_out(p).hx711_code - (-1048576.0));
                if (d > mx) mx = d;
            }
            if (k == 3) {
                CHECK_RANGE(mx, 50.0, 61.0); /* no fault: uniform +-60 */
            } else {
                CHECK_RANGE(mx, 0.85 * peaks[k], peaks[k] + 1.0);
            }
        }
        plant_init(p, 9);
        t_quiet(p);
        t_set(p, "lc_idle_noise_counts", 60.0);
        plant_fault(p, SIM_FAULT_LC_NOISE_BURST, 1, 20.0);
        plant_fault(p, SIM_FAULT_LC_NOISE_BURST, 0, 0.0);
        {
            double mx = 0.0;
            for (i = 0; i < 300; i++) {
                double d;
                t_run(p, 0, 0.1);
                d = fabs((double)t_out(p).hx711_code - (-1048576.0));
                if (d > mx) mx = d;
            }
            CHECK(mx <= 61.0); /* fault cleared */
        }
    }

    /* ---- codes are clamped to the signed 24-bit range ---- */
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "lc_baseline_code", -8300000.0);
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    t_run(p, 0, 2.0);
    CHECK(t_out(p).hx711_code == SIM_HX711_CODE_MIN);
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "lc_baseline_code", 8300000.0);
    t_set(p, "lc_counts_per_g", 7050.0);
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    t_run(p, 0, 2.0);
    CHECK(t_out(p).hx711_code == SIM_HX711_CODE_MAX);

    free(p);
    return t_finish("hx711");
}
