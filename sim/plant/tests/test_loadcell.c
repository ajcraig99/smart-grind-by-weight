#include "test_util.h"

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    int i;

    /* ---- sign convention and scale: placing 100 g lowers the code by about 705000 counts ---- */
    plant_init(p, 1); /* default noise, creep and drift on */
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    t_run(p, 0, 3.0);
    {
        double sum = 0.0;
        for (i = 0; i < 10; i++) {
            t_run(p, 0, 0.1);
            sum += (double)t_out(p).hx711_code;
        }
        CHECK_NEAR(sum / 10.0, -1048576.0 - 705000.0, 1000.0);
        CHECK(sum / 10.0 < -1048576.0); /* more mass gives a lower code */
    }
    /* exact with every stochastic and slow effect off */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    t_run(p, 0, 3.0);
    CHECK(llabs((long long)t_out(p).hx711_code - (-1048576LL - 705000LL)) <= 1);
    /* firmware view: raw offset-binary = code ^ 0x800000 stays far from the rails */
    {
        const int32_t raw = (t_out(p).hx711_code & 0xFFFFFF) ^ 0x800000;
        CHECK(raw > 0x00FFFF && raw < 0xFFFFFF - 0x00FFFF);
    }

    /* gain error scales counts per true gram */
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "lc_gain_error", 0.02);
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    t_run(p, 0, 3.0);
    CHECK(llabs((long long)t_out(p).hx711_code - (-1048576LL - (long long)llround(705000.0 * 1.02))) <= 1);

    /* ---- second-order response to a force step (PRESS has no placement impact) ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PRESS, 100.0);
    {
        double peak = 0.0, t_peak = 0.0;
        const double z = 0.30;
        const double w = 2.0 * 3.14159265358979 * 25.0;
        for (i = 0; i < 400; i++) {
            double s;
            t_run(p, 0, 0.001);
            s = t_out(p).scale_signal_g;
            if (s > peak) {
                peak = s;
                t_peak = t_out(p).t_s;
            }
        }
        CHECK_NEAR((peak - 100.0) / 100.0, exp(-3.14159265358979 * z / sqrt(1.0 - z * z)), 0.02);
        CHECK_NEAR(t_peak, 3.14159265358979 / (w * sqrt(1.0 - z * z)), 0.0015);
        t_run(p, 0, 0.5);
        CHECK_NEAR(t_out(p).scale_signal_g, 100.0, 0.01); /* settled */
        CHECK_NEAR(t_out(p).scale_true_g, 100.0, 0.0);
    }

    /* ---- creep: a fraction of a load step arrives with the creep time constant ---- */
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "lc_creep_frac", 0.0005);
    plant_action(p, SIM_ACT_PRESS, 1000.0);
    t_run(p, 0, 1.0);
    o = t_out(p);
    {
        const double early = o.scale_signal_g;
        CHECK_NEAR(early, 1000.0 * (1.0 - 0.0005) + 0.5 * (1.0 - exp(-1.0 / 30.0)), 0.02);
        t_run(p, 0, 149.0);
        o = t_out(p);
        CHECK_NEAR(o.scale_signal_g, 1000.0 - 0.5 * exp(-150.0 / 30.0), 0.01);
        CHECK(o.scale_signal_g > early + 0.4); /* crept upwards */
    }

    /* ---- linear drift ---- */
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "lc_drift_g_per_min", 0.6);
    t_run(p, 0, 60.0);
    CHECK_NEAR(t_out(p).scale_signal_g, 0.6, 1e-3);

    /* ---- random-walk drift: standard deviation after 1 min equals the parameter ---- */
    {
        double s = 0.0, s2 = 0.0;
        const int n = 300;
        int k;
        for (k = 0; k < n; k++) {
            double x;
            plant_init(p, 1000 + (uint64_t)k);
            t_quiet(p);
            t_set(p, "lc_drift_walk_g_per_rtmin", 0.05);
            t_steps(p, 0, 1, 1200, 0.05); /* dt = 50 ms: the plant works for other step sizes */
            x = t_out(p).scale_signal_g;
            s += x;
            s2 += x * x;
        }
        CHECK_NEAR(s / n, 0.0, 0.01);
        CHECK_NEAR(sqrt(s2 / n - (s / n) * (s / n)), 0.05, 0.008);
    }

    /* ---- stream impact: momentum of the falling grounds, flow * sqrt(2 g h) / g ---- */
    {
        double diff = 0.0;
        struct sim_plant* q = t_new(1);
        plant_init(p, 1);
        t_quiet(p);
        t_quiet(q);
        t_set(q, "drop_height_m", 0.0);
        plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
        plant_action(q, SIM_ACT_LOAD_BEANS, 40.0);
        t_run(p, 1, 3.0);
        t_run(q, 1, 3.0);
        diff = t_out(p).scale_signal_g - t_out(q).scale_signal_g;
        CHECK_NEAR(diff, 1.9 * sqrt(2.0 * 9.80665 * 0.10) / 9.80665, 0.005);
        free(q);
    }

    /* ---- motor vibration: proportional to speed, peak from debug.h, constant per conversion ---- */
    plant_init(p, 4);
    t_quiet(p);
    t_set(p, "lc_vibration_noise_counts", 400.0);
    {
        int prop_ok = 1;
        double mx = 0.0, s = 0.0, s2 = 0.0;
        const int n = 400;
        for (i = 0; i < 1500; i++) {
            t_run(p, 1, 0.001);
            o = t_out(p);
            if (fabs(o.scale_signal_g) > o.motor_speed * 400.0 / 7050.0 + 1e-9) prop_ok = 0;
        }
        CHECK(prop_ok); /* |vibration| <= speed * peak, including the spin-up */
        for (i = 0; i < n; i++) {
            double d;
            t_run(p, 1, 0.1);
            d = (double)t_out(p).hx711_code - (-1048576.0);
            if (fabs(d) > mx) mx = fabs(d);
            s += d;
            s2 += d * d;
        }
        CHECK_RANGE(mx, 340.0, 401.0);
        CHECK_NEAR(sqrt(s2 / n - (s / n) * (s / n)), 400.0 / sqrt(3.0), 25.0);
        /* motor off for good: no vibration */
        t_run(p, 0, 3.0);
        for (i = 0; i < 20; i++) {
            t_run(p, 0, 0.1);
            CHECK(t_out(p).hx711_code == -1048576);
        }
    }
    /* the noise-burst fault multiplies the vibration as well */
    plant_init(p, 4);
    t_quiet(p);
    t_set(p, "lc_vibration_noise_counts", 400.0);
    plant_fault(p, SIM_FAULT_LC_NOISE_BURST, 1, 5.0);
    {
        double mx = 0.0;
        t_run(p, 1, 1.0);
        for (i = 0; i < 300; i++) {
            t_run(p, 1, 0.1);
            if (fabs((double)t_out(p).hx711_code + 1048576.0) > mx) mx = fabs((double)t_out(p).hx711_code + 1048576.0);
        }
        CHECK_RANGE(mx, 1500.0, 2001.0);
    }

    /* ---- bump: a transient only, the static mass is unchanged ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_BUMP, 50.0);
    {
        double peak = 0.0;
        for (i = 0; i < 300; i++) {
            t_run(p, 0, 0.001);
            if (t_out(p).scale_signal_g > peak) peak = t_out(p).scale_signal_g;
            CHECK_NEAR(t_out(p).scale_true_g, 0.0, 0.0);
        }
        CHECK_RANGE(peak, 25.0, 50.0);
        t_run(p, 0, 0.5);
        CHECK_NEAR(t_out(p).scale_signal_g, 0.0, 0.01);
    }

    /* ---- cup placement impact rings above the final value ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 100.0);
    {
        double peak = 0.0;
        for (i = 0; i < 300; i++) {
            t_run(p, 0, 0.001);
            if (t_out(p).scale_signal_g > peak) peak = t_out(p).scale_signal_g;
        }
        CHECK(peak > 130.0);
        t_run(p, 0, 1.0);
        CHECK_NEAR(t_out(p).scale_signal_g, 100.0, 0.01);
    }

    free(p);
    return t_finish("loadcell");
}
