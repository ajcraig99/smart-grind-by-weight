/*
 * plant.c - dt-stepped physics model of the grinder, grounds path, load cell and HX711.
 * Equations are documented in README.md. All arithmetic is double precision; every transfer of
 * mass moves exactly the same value out of one store and into the next, so the total is
 * conserved to rounding error.
 */
#include "plant.h"

#include <math.h>
#include <string.h>

#define PLANT_G_STD 9.80665 /* standard gravity, m/s^2 (physical constant) */
#define PLANT_TWO_PI 6.283185307179586476925286766559
#define PLANT_EPS_T 1e-9    /* tolerance for time comparisons, s */
#define PLANT_DEFAULT_NOISE_MULT 20.0

/* ------------------------------------------------------------------ helpers */

static double clampd(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static double posd(double x) {
    return x > 0.0 ? x : 0.0;
}

static double mind(double a, double b) {
    return a < b ? a : b;
}

static double maxd(double a, double b) {
    return a > b ? a : b;
}

/* One-step gain of a first-order low pass: 1 - exp(-dt/tau). tau <= 0 means instantaneous. */
static double lp_alpha(double dt, double tau) {
    if (tau <= 1e-9) {
        return 1.0;
    }
    return 1.0 - exp(-dt / tau);
}

size_t plant_sizeof(void) {
    return sizeof(struct sim_plant);
}

static int32_t clamp_code(double v) {
    const double r = floor(v + 0.5);
    if (r <= (double)SIM_HX711_CODE_MIN) return SIM_HX711_CODE_MIN;
    if (r >= (double)SIM_HX711_CODE_MAX) return SIM_HX711_CODE_MAX;
    return (int32_t)r;
}

/* ------------------------------------------------------------------ init */

void plant_init(sim_plant_t* p, uint64_t seed) {
    struct sim_plant* s = p;
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s)); /* also zeroes padding so whole-struct memcmp is meaningful */
    plant_params_load_defaults(s->p);
    plant_rng_seed(&s->rng, seed);
    s->f_noise_mult = 1.0;
    s->in_hx_powered = 1;
    s->hx_active = 1; /* connected and powered, already settled */
    s->hx_code = clamp_code(s->p[P_LC_BASELINE]);
    s->vib_unit = plant_rng_uniform_pm1(&s->rng);
}

/* ------------------------------------------------------------------ relay */

static int relay_contact(const struct sim_plant* s) {
    if (s->f_relay_on) return 1; /* welded closed wins over stuck-off (motor-runs is the worse case) */
    if (s->f_relay_off) return 0;
    return s->relay_state;
}

static void step_relay(struct sim_plant* s, double dt) {
    const int cmd = s->in_relay_cmd != 0;
    if (cmd != s->relay_target) {
        s->relay_target = cmd;
        s->relay_timer = 0.0;
    }
    s->relay_timer += dt;
    if (s->relay_state != s->relay_target) {
        const double lat = 1e-3 * (s->relay_target ? s->p[P_RELAY_ON_MS] : s->p[P_RELAY_OFF_MS]);
        if (s->relay_timer >= lat - PLANT_EPS_T) {
            s->relay_state = s->relay_target;
        }
    }
}

/* ------------------------------------------------------------------ motor */

static int motor_powered(const struct sim_plant* s) {
    return relay_contact(s) && !s->f_motor_stall;
}

/* Returns the mean speed over the step (used to integrate the grinding output). */
static double step_motor(struct sim_plant* s, double dt) {
    const int powered = motor_powered(s);
    const double target = powered ? 1.0 : 0.0;
    double tau;
    double a;
    const double old = s->motor_speed;
    if (s->f_motor_stall) {
        tau = s->p[P_MOTOR_STALL_TAU];
    } else if (powered) {
        tau = s->p[P_MOTOR_TAU_UP];
    } else {
        tau = s->p[P_MOTOR_TAU_DOWN];
    }
    a = (tau <= 1e-9) ? 0.0 : exp(-dt / tau);
    s->motor_speed = clampd(target + (old - target) * a, 0.0, 1.0);
    return 0.5 * (old + s->motor_speed);
}

/* ------------------------------------------------------------------ hopper, burrs */

static void step_feed(struct sim_plant* s, double dt) {
    double moved;
    if (s->f_feed_block) {
        return;
    }
    moved = mind(s->m_hopper, mind(s->p[P_FEED_RATE] * dt, posd(s->p[P_BURR_CAPACITY] - s->m_burr)));
    moved = posd(moved);
    s->m_hopper -= moved;
    s->m_burr += moved;
}

/* Burr output for this step; moves the mass from the chamber into the chute store. */
static void step_burr(struct sim_plant* s, double dt, double speed_mean) {
    const double frac = s->p[P_FLOW_NOISE_FRAC];
    const double residual = posd(s->p[P_BURR_RESIDUAL]);
    const double span = s->p[P_BURR_TAPER_MASS] - residual;
    const double min_speed = s->p[P_MOTOR_MIN_SPEED];
    const double sf = (speed_mean >= min_speed && speed_mean > 0.0) ? speed_mean : 0.0;
    const double nominal = posd(s->p[P_FLOW_NOMINAL] * s->p[P_GRIND_SETTING] * s->p[P_BEAN_FACTOR]);
    const double clump_mass = posd(s->p[P_CLUMP_MASS]);
    const double clump_rate = posd(s->p[P_CLUMP_RATE]);
    double taper;
    double reg_nom;
    double mult;
    double out;
    double avail;

    /* low-pass filtered gaussian: stationary standard deviation = frac, correlation time tau */
    if (frac > 0.0) {
        const double tau = s->p[P_FLOW_NOISE_TAU];
        const double a = (tau <= 1e-9) ? 0.0 : exp(-dt / tau);
        s->flow_noise = a * s->flow_noise + sqrt(posd(1.0 - a * a)) * frac * plant_rng_gauss(&s->rng);
    } else {
        s->flow_noise = 0.0;
    }
    mult = clampd(1.0 + s->flow_noise, 0.0, 4.0);

    if (span > 1e-9) {
        taper = clampd((s->m_burr - residual) / span, 0.0, 1.0);
    } else {
        taper = (s->m_burr > residual) ? 1.0 : 0.0;
    }

    /* the regular flow is reduced by the mean clump flow so that the mean total stays nominal */
    reg_nom = posd(nominal - clump_rate * clump_mass);
    out = reg_nom * sf * taper * mult * dt;
    if (sf > 0.0 && taper > 0.0 && clump_rate > 0.0 && clump_mass > 0.0) {
        const double prob = 1.0 - exp(-clump_rate * sf * taper * dt);
        if (plant_rng_uniform(&s->rng) < prob) {
            out += clump_mass * plant_rng_exp1(&s->rng);
        }
    }

    avail = posd(s->m_burr - residual); /* the residual never grinds */
    if (out > avail) {
        out = avail;
    }
    s->m_burr -= out;
    s->m_chute += out;
    s->flow_burr_gps = out / dt;
}

/* ------------------------------------------------------------------ chute and transport */

/* Drains the chute excess above the static retention; returns the mass leaving the chute. */
static double step_chute(struct sim_plant* s, double dt) {
    const double retention = posd(s->p[P_CHUTE_RETENTION]);
    const double excess = posd(s->m_chute - retention);
    const double out = excess * lp_alpha(dt, s->p[P_CHUTE_TAU]);
    s->m_chute -= out;
    return out;
}

static void ring_deposit(struct sim_plant* s, double mass) {
    const double delay = clampd(s->p[P_TRANSPORT_DELAY], 0.0, PLANT_RING_MAX_DELAY_BINS * PLANT_RING_BIN_S);
    int64_t bin;
    if (mass <= 0.0) {
        return;
    }
    bin = (int64_t)floor((s->t + delay) / PLANT_RING_BIN_S + 1e-4);
    if (bin < s->ring_read) {
        bin = s->ring_read;
    }
    if (bin > s->ring_read + (PLANT_RING_BINS - 1)) {
        bin = s->ring_read + (PLANT_RING_BINS - 1); /* only reachable with absurdly large dt */
    }
    s->ring[(size_t)(bin & (PLANT_RING_BINS - 1))] += mass;
}

/* Releases every bin that has come due; returns the mass arriving at the cup/platform. */
static double ring_release(struct sim_plant* s) {
    const int64_t target = (int64_t)floor(s->t / PLANT_RING_BIN_S + 1e-4);
    double arrived = 0.0;
    if (target - s->ring_read >= PLANT_RING_BINS) {
        int i;
        for (i = 0; i < PLANT_RING_BINS; i++) {
            arrived += s->ring[i];
            s->ring[i] = 0.0;
        }
        s->ring_read = target + 1;
        return arrived;
    }
    while (s->ring_read <= target) {
        const size_t idx = (size_t)(s->ring_read & (PLANT_RING_BINS - 1));
        arrived += s->ring[idx];
        s->ring[idx] = 0.0;
        s->ring_read++;
    }
    return arrived;
}

static double ring_sum(const struct sim_plant* s) {
    double sum = 0.0;
    int i;
    for (i = 0; i < PLANT_RING_BINS; i++) {
        sum += s->ring[i];
    }
    return sum;
}

/* ------------------------------------------------------------------ load cell */

static double static_load(const struct sim_plant* s) {
    return (s->cup_present ? s->cup_vessel_g + s->m_cup : 0.0) + s->m_platform + s->press_g;
}

static void step_loadcell(struct sim_plant* s, double dt) {
    const double u_static = static_load(s);
    const double creep_frac = s->p[P_LC_CREEP_FRAC];
    const double fn = s->p[P_LC_NAT_FREQ];
    const double cpg_abs = fabs(s->p[P_LC_COUNTS_PER_G]);
    double u;
    double stream;
    double vib_g;

    /* creep: a small fraction of the load arrives through a slow first-order path */
    s->lc_creep_lp += (u_static - s->lc_creep_lp) * lp_alpha(dt, s->p[P_LC_CREEP_TAU]);
    u = (1.0 - creep_frac) * u_static + creep_frac * s->lc_creep_lp;

    /* momentum of the falling stream: F = flow * v / g (in gram-equivalents), v = sqrt(2 g h) */
    stream = s->flow_cup_gps * sqrt(2.0 * PLANT_G_STD * posd(s->p[P_DROP_HEIGHT])) / PLANT_G_STD;

    /* placement and bump transients decay exponentially */
    {
        const double tau = s->p[P_LC_IMPACT_TAU];
        s->impact_g *= (tau <= 1e-9) ? 0.0 : exp(-dt / tau);
    }
    u += stream + s->impact_g;

    /* lightly damped second-order response x'' + 2 z w x' + w^2 x = w^2 u (sub-stepped) */
    if (fn > 1e-6) {
        const double w = PLANT_TWO_PI * fn;
        const double z = s->p[P_LC_DAMPING];
        int n = (int)ceil(w * dt / 0.05);
        double h;
        int i;
        if (n < 1) n = 1;
        if (n > 400) n = 400;
        h = dt / (double)n;
        for (i = 0; i < n; i++) {
            const double acc = w * w * (u - s->lc_x) - 2.0 * z * w * s->lc_v;
            s->lc_v += acc * h;
            s->lc_x += s->lc_v * h;
        }
    } else {
        s->lc_x = u;
        s->lc_v = 0.0;
    }

    /* zero drift: linear slope plus random walk */
    s->drift_lin_g += s->p[P_LC_DRIFT_LIN] / 60.0 * dt;
    if (s->p[P_LC_DRIFT_WALK] > 0.0) {
        s->drift_walk_g += s->p[P_LC_DRIFT_WALK] * sqrt(dt / 60.0) * plant_rng_gauss(&s->rng);
    }

    /* motor vibration, constant over one conversion period (band-limited to the ADC bandwidth) */
    vib_g = 0.0;
    if (cpg_abs > 1e-9) {
        vib_g = s->motor_speed * s->vib_unit * s->p[P_LC_VIB_NOISE] * s->f_noise_mult / cpg_abs;
    }

    s->scale_signal_g = s->lc_x + s->drift_lin_g + s->drift_walk_g + vib_g;
}

/* ------------------------------------------------------------------ HX711 */

static void step_hx711(struct sim_plant* s, double dt) {
    const int want = s->in_hx_powered && !s->f_lc_disconnect;
    const double sps = s->p[P_HX711_SPS];
    const double period = 1.0 / (sps >= 1.0 ? sps : 1.0);

    if (!want) {
        s->hx_active = 0;
        s->hx_ready = 0;
        return;
    }
    if (!s->hx_active) {
        /* power-up: the first conversion completes after the output settling time */
        const double settle = 1e-3 * (sps >= 40.0 ? s->p[P_HX711_SETTLE_80] : s->p[P_HX711_SETTLE_10]);
        s->hx_active = 1;
        s->hx_ready = 0;
        s->hx_phase = period - settle;
        s->hx_acc = 0.0;
        s->hx_acc_t = 0.0;
    }

    s->hx_phase += dt;
    if (s->hx_phase >= 0.0) {
        s->hx_acc += s->scale_signal_g * dt;
        s->hx_acc_t += dt;
    }
    if (s->hx_phase >= period - PLANT_EPS_T) {
        const double avg = (s->hx_acc_t > 1e-12) ? s->hx_acc / s->hx_acc_t : s->scale_signal_g;
        const double idle_peak = s->p[P_LC_IDLE_NOISE] * s->f_noise_mult;
        const double noise = plant_rng_uniform_pm1(&s->rng) * idle_peak;
        const double code_f = s->p[P_LC_BASELINE] + avg * s->p[P_LC_COUNTS_PER_G] * (1.0 + s->p[P_LC_GAIN_ERROR]) + noise;
        if (!s->f_lc_stuck) {
            s->hx_code = clamp_code(code_f);
        }
        s->hx_seq++;
        s->hx_ready = 1; /* a new conversion overwrites an unconsumed one */
        s->vib_unit = plant_rng_uniform_pm1(&s->rng);
        s->hx_phase = maxd(0.0, s->hx_phase - period);
        s->hx_acc = 0.0;
        s->hx_acc_t = 0.0;
    }
}

/* ------------------------------------------------------------------ step */

void plant_step(sim_plant_t* p, const sim_plant_inputs_t* in, double dt_s) {
    struct sim_plant* s = p;
    double speed_mean;
    double chute_out;
    double arrived;
    if (s == NULL || !(dt_s > 0.0)) {
        return;
    }
    s->in_relay_cmd = (in != NULL) ? (in->relay_cmd != 0) : 0;
    s->in_hx_powered = (in != NULL) ? (in->hx711_powered != 0) : 1;
    s->t += dt_s;

    /* the motor sees the contact state from the start of the step, so a contact that closes at
     * t = cmd + latency powers the motor from the following step */
    speed_mean = step_motor(s, dt_s);
    step_relay(s, dt_s);
    step_feed(s, dt_s);
    step_burr(s, dt_s, speed_mean);
    chute_out = step_chute(s, dt_s);
    ring_deposit(s, chute_out);
    arrived = ring_release(s);
    if (s->cup_present) {
        s->m_cup += arrived;
    } else {
        s->m_platform += arrived;
    }
    s->flow_cup_gps = arrived / dt_s;
    step_loadcell(s, dt_s);
    step_hx711(s, dt_s);
}

/* ------------------------------------------------------------------ outputs */

void plant_get_outputs(const sim_plant_t* p, sim_plant_outputs_t* out) {
    const struct sim_plant* s = p;
    double inflight;
    double sum;
    if (s == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    inflight = ring_sum(s);
    out->t_s = s->t;
    out->relay_contact = relay_contact(s);
    out->motor_speed = s->motor_speed;
    out->motor_powered = motor_powered(s);
    out->motor_stalled = s->f_motor_stall != 0;
    out->flow_burr_gps = s->flow_burr_gps;
    out->flow_cup_gps = s->flow_cup_gps;
    out->m_loaded_g = s->m_loaded;
    out->m_hopper_g = s->m_hopper;
    out->m_burr_g = s->m_burr;
    out->m_chute_g = s->m_chute;
    out->m_inflight_g = inflight;
    out->m_cup_g = s->m_cup;
    out->m_platform_g = s->m_platform;
    out->m_spilled_g = s->m_spilled;
    sum = s->m_hopper + s->m_burr + s->m_chute + inflight + s->m_cup + s->m_platform + s->m_spilled;
    out->conservation_error_g = s->m_loaded - sum;
    out->cup_present = s->cup_present != 0;
    out->cup_mass_g = s->cup_exists ? s->cup_vessel_g : s->p[P_CUP_MASS];
    out->scale_true_g = static_load(s);
    out->scale_signal_g = s->scale_signal_g;
    out->hx711_connected = !s->f_lc_disconnect;
    out->hx711_powered = s->in_hx_powered;
    out->hx711_ready = s->hx_ready && s->hx_active;
    out->hx711_code = s->hx_code;
    out->hx711_seq = s->hx_seq;
    out->hx711_sps = s->p[P_HX711_SPS];
}

void plant_hx711_consume(sim_plant_t* p) {
    if (p != NULL) {
        p->hx_ready = 0;
    }
}

/* ------------------------------------------------------------------ actions and faults */

void plant_action(sim_plant_t* p, int action, double value) {
    struct sim_plant* s = p;
    if (s == NULL) {
        return;
    }
    switch (action) {
    case SIM_ACT_PLACE_CUP:
        if (!s->cup_present) {
            /* the same cup returns with its contents after a lift; a fresh one uses the default */
            double vessel = s->p[P_CUP_MASS];
            if (value > 0.0) {
                vessel = value;
            } else if (s->cup_exists) {
                vessel = s->cup_vessel_g;
            }
            s->cup_vessel_g = vessel;
            s->cup_present = 1;
            s->cup_exists = 1;
            s->impact_g += s->p[P_LC_IMPACT_FRAC] * (vessel + s->m_cup);
        } /* already on the platform: nothing happens */
        break;
    case SIM_ACT_REMOVE_CUP:
        s->cup_present = 0; /* contents (m_cup) go with it */
        break;
    case SIM_ACT_EMPTY_CUP:
        if (!s->cup_present) {
            s->m_spilled += s->m_cup;
            s->m_cup = 0.0;
        } /* cup on the platform: the caller must lift, empty, replace */
        break;
    case SIM_ACT_LOAD_BEANS:
        if (value > 0.0) {
            s->m_hopper += value;
            s->m_loaded += value;
        }
        break;
    case SIM_ACT_BUMP:
        s->impact_g += value;
        break;
    case SIM_ACT_PRESS:
        s->press_g = value > 0.0 ? value : 0.0;
        break;
    case SIM_ACT_CLEAN_CHUTE:
        s->m_spilled += s->m_chute;
        s->m_chute = 0.0;
        break;
    case SIM_ACT_WIPE_PLATFORM:
        s->m_spilled += s->m_platform;
        s->m_platform = 0.0;
        break;
    default:
        break;
    }
}

void plant_fault(sim_plant_t* p, int fault, int active, double value) {
    struct sim_plant* s = p;
    const int on = active != 0;
    if (s == NULL) {
        return;
    }
    switch (fault) {
    case SIM_FAULT_LC_DISCONNECT:
        s->f_lc_disconnect = on;
        if (on) {
            s->hx_active = 0;
            s->hx_ready = 0;
        }
        break;
    case SIM_FAULT_LC_STUCK:
        s->f_lc_stuck = on; /* hx_code already holds the last value; it stays frozen */
        break;
    case SIM_FAULT_LC_NOISE_BURST:
        s->f_noise_active = on;
        s->f_noise_mult = on ? (value > 0.0 ? value : PLANT_DEFAULT_NOISE_MULT) : 1.0;
        break;
    case SIM_FAULT_RELAY_STUCK_ON:
        s->f_relay_on = on;
        break;
    case SIM_FAULT_RELAY_STUCK_OFF:
        s->f_relay_off = on;
        break;
    case SIM_FAULT_MOTOR_STALL:
        s->f_motor_stall = on;
        break;
    case SIM_FAULT_FEED_BLOCK:
        s->f_feed_block = on;
        break;
    default:
        break;
    }
}
