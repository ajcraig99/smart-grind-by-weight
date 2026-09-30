#include "test_util.h"

/* Number of 1 ms steps after which the contact state first equals `want`, or -1. */
static int steps_until_contact(struct sim_plant* p, int relay, int want, int max_steps) {
    int i;
    for (i = 1; i <= max_steps; i++) {
        t_steps(p, relay, 1, 1, SIM_PLANT_DT_S);
        if (t_out(p).relay_contact == want) return i;
    }
    return -1;
}

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    int i;

    /* on latency: contact closes 10 ms after the command, not before */
    CHECK(t_out(p).relay_contact == 0);
    CHECK(steps_until_contact(p, 1, 1, 100) == 10);
    /* the motor sees the contact from the following step */
    o = t_out(p);
    CHECK(o.motor_speed == 0.0);
    CHECK(o.motor_powered == 1);

    /* off latency: 8 ms */
    CHECK(steps_until_contact(p, 0, 0, 100) == 8);

    /* a command shorter than the latency never closes the contact */
    t_steps(p, 1, 1, 5, SIM_PLANT_DT_S);
    for (i = 0; i < 50; i++) {
        t_steps(p, 0, 1, 1, SIM_PLANT_DT_S);
        CHECK(t_out(p).relay_contact == 0);
    }
    /* a release shorter than the off latency does not open a closed contact */
    CHECK(steps_until_contact(p, 1, 1, 100) == 10);
    t_steps(p, 0, 1, 4, SIM_PLANT_DT_S);
    CHECK(t_out(p).relay_contact == 1);
    for (i = 0; i < 50; i++) {
        t_steps(p, 1, 1, 1, SIM_PLANT_DT_S);
        CHECK(t_out(p).relay_contact == 1);
    }

    /* parameter change and step size: 20 ms on-latency at dt = 0.5 ms takes 40 steps */
    plant_init(p, 1);
    t_set(p, "relay_on_latency_ms", 20.0);
    t_set(p, "relay_off_latency_ms", 3.0);
    CHECK(steps_until_contact(p, 1, 1, 200) == 20);
    CHECK(steps_until_contact(p, 0, 0, 200) == 3);
    plant_init(p, 1);
    t_set(p, "relay_on_latency_ms", 20.0);
    for (i = 1; i <= 100; i++) {
        t_steps(p, 1, 1, 1, 0.0005);
        if (t_out(p).relay_contact) break;
    }
    CHECK(i == 40);

    /* stuck on: contact closed although the command is low; motor powered */
    plant_init(p, 1);
    plant_fault(p, SIM_FAULT_RELAY_STUCK_ON, 1, 0.0);
    t_steps(p, 0, 1, 1, SIM_PLANT_DT_S);
    o = t_out(p);
    CHECK(o.relay_contact == 1);
    CHECK(o.motor_powered == 1);
    t_run(p, 0, 1.0);
    CHECK_NEAR(t_out(p).motor_speed, 1.0, 1e-3);
    plant_fault(p, SIM_FAULT_RELAY_STUCK_ON, 0, 0.0);
    t_steps(p, 0, 1, 1, SIM_PLANT_DT_S);
    CHECK(t_out(p).relay_contact == 0); /* follows the (never energised) relay again */

    /* stuck off: commanded on, contact never closes, motor never moves */
    plant_init(p, 1);
    plant_fault(p, SIM_FAULT_RELAY_STUCK_OFF, 1, 0.0);
    t_run(p, 1, 1.0);
    o = t_out(p);
    CHECK(o.relay_contact == 0);
    CHECK(o.motor_powered == 0);
    CHECK(o.motor_speed == 0.0);
    /* releasing the fault: the relay had already closed internally, so the contact closes at once */
    plant_fault(p, SIM_FAULT_RELAY_STUCK_OFF, 0, 0.0);
    CHECK(t_out(p).relay_contact == 1);

    /* both stuck faults: stuck-on takes precedence (documented worst case) */
    plant_init(p, 1);
    plant_fault(p, SIM_FAULT_RELAY_STUCK_OFF, 1, 0.0);
    plant_fault(p, SIM_FAULT_RELAY_STUCK_ON, 1, 0.0);
    CHECK(t_out(p).relay_contact == 1);

    free(p);
    return t_finish("relay");
}
