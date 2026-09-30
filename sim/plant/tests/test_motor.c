#include "test_util.h"

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    const double tau_up = 0.08, tau_down = 0.10, tau_stall = 0.02;

    /* spin-up: contact closes at 10 ms, then first order with tau_up */
    t_quiet(p);
    t_steps(p, 1, 1, 10, SIM_PLANT_DT_S);
    CHECK_NEAR(t_out(p).motor_speed, 0.0, 1e-12);
    t_steps(p, 1, 1, 80, SIM_PLANT_DT_S); /* t = 90 ms = 10 ms + tau_up */
    CHECK_NEAR(t_out(p).motor_speed, 1.0 - exp(-1.0), 2e-3);
    t_steps(p, 1, 1, 80, SIM_PLANT_DT_S); /* t = 170 ms = 10 ms + 2 tau_up */
    CHECK_NEAR(t_out(p).motor_speed, 1.0 - exp(-2.0), 2e-3);
    t_run(p, 1, 2.0);
    CHECK_NEAR(t_out(p).motor_speed, 1.0, 1e-6);

    /* coast-down: the contact opens 8 ms after the command, then first order with tau_down */
    t_steps(p, 0, 1, 8, SIM_PLANT_DT_S);
    CHECK(t_out(p).relay_contact == 0);
    CHECK(t_out(p).motor_speed > 0.999);
    t_steps(p, 0, 1, 100, SIM_PLANT_DT_S); /* one tau_down later */
    CHECK_NEAR(t_out(p).motor_speed, exp(-1.0), 3e-3);
    t_steps(p, 0, 1, 100, SIM_PLANT_DT_S);
    CHECK_NEAR(t_out(p).motor_speed, exp(-2.0), 3e-3);

    /* the time constants are parameters; dt does not change the result */
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "motor_tau_up_s", 0.2);
    t_set(p, "relay_on_latency_ms", 5.0);
    t_steps(p, 1, 1, 10, 0.0005);   /* contact closes after 5 ms */
    t_steps(p, 1, 1, 400, 0.0005);  /* 200 ms = tau_up */
    CHECK_NEAR(t_out(p).motor_speed, 1.0 - exp(-1.0), 3e-3);

    /* stall: powered but speed collapses with the stall time constant; grinding stops */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 1, 2.0);
    CHECK_NEAR(t_out(p).motor_speed, 1.0, 1e-6);
    CHECK(t_out(p).flow_burr_gps > 1.8);
    plant_fault(p, SIM_FAULT_MOTOR_STALL, 1, 0.0);
    t_steps(p, 1, 1, 20, SIM_PLANT_DT_S);
    o = t_out(p);
    CHECK_NEAR(o.motor_speed, exp(-1.0), 3e-3);
    CHECK(o.motor_stalled == 1);
    CHECK(o.motor_powered == 0);
    CHECK(o.relay_contact == 1);
    (void)tau_stall;
    t_run(p, 1, 0.5);
    o = t_out(p);
    CHECK(o.motor_speed < 1e-6);
    CHECK_NEAR(o.flow_burr_gps, 0.0, 1e-12);
    /* clearing the stall: the motor spins up again and grinding resumes */
    plant_fault(p, SIM_FAULT_MOTOR_STALL, 0, 0.0);
    t_run(p, 1, 1.0);
    o = t_out(p);
    CHECK(o.motor_stalled == 0);
    CHECK_NEAR(o.motor_speed, 1.0, 1e-3);
    CHECK(o.flow_burr_gps > 1.8);

    /* the burrs keep grinding while the motor coasts: flow after the contact opens is nonzero */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 1, 2.0);
    t_steps(p, 0, 1, 60, SIM_PLANT_DT_S); /* 60 ms after the command, contact open since 8 ms */
    o = t_out(p);
    CHECK(o.relay_contact == 0);
    CHECK(o.flow_burr_gps > 0.5);
    (void)tau_up;
    (void)tau_down;

    free(p);
    return t_finish("motor");
}
