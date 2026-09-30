/*
 * Timing targets the default parameters are tuned to (brief: "Plant model"):
 *   - shortest productive pulse about 40-50 ms (the firmware mock hides 42 ms, src/config/debug.h:45)
 *   - continuous start-to-first-grounds latency roughly 0.4-0.6 s (mock 500 ms, debug.h:43)
 *   - stop-to-flow-stop roughly 0.4-0.5 s (mock 400 ms, debug.h:44)
 * The flow-detection threshold 0.5 g/s is the firmware's own latency criterion
 * (src/config/grind_control.h:49, GRIND_FLOW_DETECTION_THRESHOLD_GPS).
 */
#include "test_util.h"

#define THRESH_GPS 0.5

/* Seconds from the start command until the flow at the cup reaches 0.5 g/s. */
static double start_latency(int primed_chute) {
    struct sim_plant* p = t_new(1);
    sim_plant_inputs_t in = {1, 1};
    double lat = -1.0;
    int i;
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    if (primed_chute) {
        t_run(p, 1, 3.0); /* grind, then let everything drain: the chute keeps its retention */
        t_run(p, 0, 3.0);
    } else {
        t_run(p, 0, 1.0);
    }
    for (i = 1; i <= 2000; i++) {
        plant_step(p, &in, SIM_PLANT_DT_S);
        if (t_out(p).flow_cup_gps >= THRESH_GPS) {
            lat = i * SIM_PLANT_DT_S;
            break;
        }
    }
    free(p);
    return lat;
}

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_inputs_t off = {0, 1};
    double last_half = 0.0, last_twentieth = 0.0;
    int i;

    const double primed = start_latency(1);
    const double empty = start_latency(0);
    printf("start latency to %.1f g/s: primed chute %.3f s, empty chute %.3f s\n", THRESH_GPS, primed, empty);
    CHECK_RANGE(primed, 0.40, 0.60);
    CHECK_RANGE(empty, 0.40, 0.60);
    CHECK(empty > primed); /* an empty chute first has to fill its retention */

    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 1, 3.0);
    for (i = 1; i <= 2000; i++) {
        const double f = (plant_step(p, &off, SIM_PLANT_DT_S), t_out(p).flow_cup_gps);
        if (f >= THRESH_GPS) last_half = i * SIM_PLANT_DT_S;
        if (f > 0.05) last_twentieth = i * SIM_PLANT_DT_S;
    }
    printf("stop latency: flow at cup below %.2f g/s after %.3f s, below 0.05 g/s after %.3f s\n",
           THRESH_GPS, last_half, last_twentieth);
    CHECK_RANGE(last_half, 0.40, 0.55);
    CHECK_RANGE(last_twentieth, 0.50, 0.70);

    /* grounds per pulse follow the firmware mock's 1.9 g/s * (duration - 42 ms) */
    for (i = 50; i <= 120; i += 10) {
        struct sim_plant* q = t_new(1);
        t_quiet(q);
        plant_action(q, SIM_ACT_LOAD_BEANS, 30.0);
        t_run(q, 0, 1.0);
        t_steps(q, 1, 1, i, SIM_PLANT_DT_S);
        t_run(q, 0, 2.0);
        printf("pulse %3d ms: %.4f g (mock %.4f g)\n", i, t_ground_out(q), 1.9 * (i - 42) / 1000.0);
        CHECK_NEAR(t_ground_out(q), 1.9 * (i - 42) / 1000.0, 0.02);
        free(q);
    }

    free(p);
    return t_finish("timing");
}
