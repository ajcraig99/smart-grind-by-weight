/*
 * plant.h - private layout of the plant model (sim/plant, C11, no dependencies beyond libc/libm).
 *
 * struct sim_plant is plain old data: doubles, integers and fixed-size arrays of those, NO
 * pointers. The whole state (parameters, RNG, every mass, the transport ring buffer) can be
 * copied with memcpy to save and restore a run; the copy continues identically. There is no
 * global mutable state and no wall clock: everything advances only through plant_step().
 *
 * Public contract: ../sim_api.h section 1. Everything below is for the plant itself and its tests.
 */
#ifndef PLANT_H
#define PLANT_H

#include "../sim_api.h"
#include "plant_params.h"
#include "rng.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Transport delay ring buffer: fixed 1 ms bins (independent of the step size dt). */
#define PLANT_RING_BINS 1024
#define PLANT_RING_BIN_S 0.001
#define PLANT_RING_MAX_DELAY_BINS 1000 /* delay cap = 1.0 s; transport_delay_s is clamped to it */

/* Extension beyond sim_api.h's enum (request to add it there): wipe grounds off the bare platform. */
#ifndef SIM_ACT_WIPE_PLATFORM
#define SIM_ACT_WIPE_PLATFORM 8
#endif

struct sim_plant {
    double p[PLANT_PARAM_COUNT]; /* live parameter values, indexed by enum plant_param_id */
    plant_rng_t rng;
    double t;                    /* plant time since init, s */

    /* inputs latched from the last step */
    int in_relay_cmd;
    int in_hx_powered;

    /* faults */
    int f_lc_disconnect;
    int f_lc_stuck;
    int f_relay_on;
    int f_relay_off;
    int f_motor_stall;
    int f_feed_block;
    int f_noise_active;
    double f_noise_mult;

    /* relay */
    int relay_target;            /* last commanded level seen */
    int relay_state;             /* internal contact state after latency (before stuck overrides) */
    double relay_timer;          /* s since the command last changed */

    /* motor */
    double motor_speed;          /* 0..1 */

    /* masses, g */
    double m_loaded;
    double m_hopper;
    double m_burr;
    double m_chute;
    double m_cup;                /* grounds inside the cup, wherever the cup is */
    double m_platform;
    double m_spilled;
    double flow_noise;           /* low-pass filtered multiplicative noise, zero mean */
    double flow_burr_gps;        /* last step */
    double flow_cup_gps;         /* last step */

    /* transport ring: mass per 1 ms bin, indexed by absolute bin number modulo PLANT_RING_BINS */
    int64_t ring_read;           /* next absolute bin to release */
    double ring[PLANT_RING_BINS];

    /* cup */
    int cup_present;             /* on the platform */
    int cup_exists;              /* a cup has been placed at least once (may be off the platform) */
    double cup_vessel_g;         /* empty vessel mass of the cup in use */
    double press_g;

    /* load cell mechanics */
    double lc_creep_lp;          /* slow low-pass of the static load (creep) */
    double lc_x;                 /* second-order response output, g */
    double lc_v;                 /* and its rate, g/s */
    double impact_g;             /* decaying placement/bump transient force, g */
    double drift_lin_g;
    double drift_walk_g;
    double vib_unit;             /* uniform (-1,1), refreshed once per conversion period */
    double scale_signal_g;

    /* HX711 */
    int hx_active;               /* powered and connected */
    int hx_ready;
    int32_t hx_code;
    uint32_t hx_seq;
    double hx_phase;             /* s into the conversion period; negative while settling */
    double hx_acc;               /* integral of the signal over the current period, g*s */
    double hx_acc_t;             /* time covered by hx_acc, s */
};

/* Parameter table helpers (plant_params.c). */
int plant_param_index(const char* name);              /* -1 if unknown */
void plant_params_load_defaults(double* values);      /* PLANT_PARAM_COUNT doubles */

/* plant_set_param returns -2 (extension of sim_api.h's 0/-1) for a known name with a non-finite value. */

#ifdef __cplusplus
}
#endif

#endif /* PLANT_H */
