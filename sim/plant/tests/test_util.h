/* test_util.h - tiny check framework and helpers shared by the plant unit tests. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plant.h"

static int t_fail = 0;
static int t_checks = 0;

#define CHECK(cond)                                                                     \
    do {                                                                                \
        t_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
            t_fail++;                                                                   \
        }                                                                               \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                               \
    do {                                                                                \
        const double a_ = (actual);                                                     \
        const double e_ = (expected);                                                   \
        const double t_ = (tol);                                                        \
        t_checks++;                                                                     \
        if (!(fabs(a_ - e_) <= t_)) {                                                   \
            printf("FAIL %s:%d: %s = %.9g, expected %.9g +- %.3g\n", __FILE__, __LINE__, \
                   #actual, a_, e_, t_);                                                \
            t_fail++;                                                                   \
        }                                                                               \
    } while (0)

#define CHECK_RANGE(actual, lo, hi)                                                     \
    do {                                                                                \
        const double a_ = (actual);                                                     \
        t_checks++;                                                                     \
        if (!(a_ >= (lo) && a_ <= (hi))) {                                              \
            printf("FAIL %s:%d: %s = %.9g, expected in [%.9g, %.9g]\n", __FILE__, __LINE__, \
                   #actual, a_, (double)(lo), (double)(hi));                            \
            t_fail++;                                                                   \
        }                                                                               \
    } while (0)

static inline int t_finish(const char* name) {
    printf("%s: %d checks, %d failed\n", name, t_checks, t_fail);
    return t_fail == 0 ? 0 : 1;
}

/* Plants are large (about 9 kB): keep them out of the stack frames of the tests. */
static inline struct sim_plant* t_new(uint64_t seed) {
    struct sim_plant* p = (struct sim_plant*)malloc(sizeof(struct sim_plant));
    if (p == NULL) {
        printf("out of memory\n");
        exit(2);
    }
    plant_init(p, seed);
    return p;
}

static inline double t_get(const struct sim_plant* p, const char* name) {
    int ok = 0;
    const double v = plant_get_param(p, name, &ok);
    if (!ok) {
        printf("FAIL: unknown parameter %s\n", name);
        t_fail++;
    }
    return v;
}

static inline void t_set(struct sim_plant* p, const char* name, double v) {
    if (plant_set_param(p, name, v) != 0) {
        printf("FAIL: cannot set parameter %s\n", name);
        t_fail++;
    }
}

/* Remove every stochastic and slow effect so exact equations can be checked. */
static inline void t_quiet(struct sim_plant* p) {
    t_set(p, "flow_noise_frac", 0.0);
    t_set(p, "clump_rate_hz", 0.0);
    t_set(p, "lc_idle_noise_counts", 0.0);
    t_set(p, "lc_vibration_noise_counts", 0.0);
    t_set(p, "lc_drift_g_per_min", 0.0);
    t_set(p, "lc_drift_walk_g_per_rtmin", 0.0);
    t_set(p, "lc_creep_frac", 0.0);
}

static inline sim_plant_outputs_t t_out(const struct sim_plant* p) {
    sim_plant_outputs_t o;
    plant_get_outputs(p, &o);
    return o;
}

/* Step n times with the given inputs at dt. */
static inline void t_steps(struct sim_plant* p, int relay, int powered, int n, double dt) {
    sim_plant_inputs_t in;
    int i;
    in.relay_cmd = relay;
    in.hx711_powered = powered;
    for (i = 0; i < n; i++) {
        plant_step(p, &in, dt);
    }
}

/* Run for `seconds` at dt = 1 ms. */
static inline void t_run(struct sim_plant* p, int relay, double seconds) {
    t_steps(p, relay, 1, (int)(seconds * 1000.0 + 0.5), SIM_PLANT_DT_S);
}

/* Total grounds that have left the burr chamber (chute + in flight + cup + platform + spilled). */
static inline double t_ground_out(const struct sim_plant* p) {
    const sim_plant_outputs_t o = t_out(p);
    return o.m_chute_g + o.m_inflight_g + o.m_cup_g + o.m_platform_g + o.m_spilled_g;
}

#endif /* TEST_UTIL_H */
