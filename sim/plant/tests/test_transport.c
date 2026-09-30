#include "test_util.h"

/* Time from the relay command to the first grounds arriving at the cup, with the given dt. */
static double first_arrival(const char* name, double value, double dt) {
    struct sim_plant* p = t_new(1);
    sim_plant_inputs_t in;
    double t_cmd, t_first = -1.0;
    int i;
    t_quiet(p);
    t_set(p, "chute_retention_g", 0.0);
    t_set(p, "chute_tau_s", 0.0005);
    if (name != NULL) t_set(p, name, value);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_steps(p, 0, 1, (int)(1.0 / dt), dt);
    in.relay_cmd = 1;
    in.hx711_powered = 1;
    t_cmd = t_out(p).t_s;
    for (i = 0; i < (int)(3.0 / dt); i++) {
        plant_step(p, &in, dt);
        if (t_out(p).flow_cup_gps > 0.0) {
            t_first = t_out(p).t_s - t_cmd;
            break;
        }
    }
    free(p);
    return t_first;
}

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    const double fixed = 0.0509 + 0.001; /* relay 10 ms + reach motor_min_speed, plus chute step */

    /* first arrival = relay + spin-up to min speed + transport delay */
    CHECK_NEAR(first_arrival(NULL, 0, 0.001), fixed + 0.32, 0.004);
    CHECK_NEAR(first_arrival("transport_delay_s", 0.10, 0.001), fixed + 0.10, 0.004);
    CHECK_NEAR(first_arrival("transport_delay_s", 0.60, 0.001), fixed + 0.60, 0.004);
    CHECK(first_arrival("transport_delay_s", 0.0, 0.001) < 0.07);
    /* other step sizes give the same physics */
    CHECK_NEAR(first_arrival(NULL, 0, 0.0005), fixed + 0.32, 0.005);
    CHECK_NEAR(first_arrival(NULL, 0, 0.004), fixed + 0.32, 0.012);
    /* the ring buffer is capped at 1.0 s; larger values behave as 1.0 s */
    CHECK_NEAR(first_arrival("transport_delay_s", 5.0, 0.001), fixed + 1.0, 0.006);

    /* in-flight mass in steady state = flow * delay */
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
    t_run(p, 1, 3.0);
    o = t_out(p);
    CHECK_NEAR(o.m_inflight_g, 1.9 * 0.32, 0.01);
    CHECK_NEAR(o.flow_cup_gps, 1.9, 2e-3);
    CHECK(fabs(o.conservation_error_g) < 1e-12);

    /* arrivals go into the cup when present, otherwise onto the bare platform */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
    t_run(p, 1, 3.0);
    o = t_out(p);
    CHECK(o.m_platform_g > 1.0);
    CHECK_NEAR(o.m_cup_g, 0.0, 0.0);
    CHECK_NEAR(o.scale_true_g, o.m_platform_g, 1e-12);

    /* grounds already in flight keep falling after the motor stops and the cup is lifted off:
     * they land on the platform */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 40.0);
    t_run(p, 1, 3.0);
    plant_action(p, SIM_ACT_REMOVE_CUP, 0.0);
    {
        const double in_cup = t_out(p).m_cup_g;
        t_run(p, 1, 1.0);
        o = t_out(p);
        CHECK_NEAR(o.m_cup_g, in_cup, 0.0); /* contents left with the cup */
        CHECK(o.m_platform_g > 1.0);
    }

    free(p);
    return t_finish("transport");
}
