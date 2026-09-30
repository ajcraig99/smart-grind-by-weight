/*
 * sim_api.h - boundaries of the digital twin.
 *
 *  1. Plant model <-> shims/runtime (implemented in sim/plant, C11).
 *  2. Runtime <-> host CLI and JavaScript (implemented in sim/core, exported from WASM).
 *
 * Plain C, no pointers inside plant state (the state is memcpy-able so it can be saved and
 * restored around a simulated firmware reset). All times are virtual; nothing here reads a
 * wall clock.
 */
#ifndef SIM_API_H
#define SIM_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================================
 * 1. PLANT
 * ====================================================================================== */

/* Fixed plant step. The runtime calls plant_step once per virtual millisecond. */
#define SIM_PLANT_DT_S 0.001

/* HX711 signed 24-bit range (two's complement as shifted out on DOUT). */
#define SIM_HX711_CODE_MIN (-8388608)
#define SIM_HX711_CODE_MAX (8388607)

typedef struct sim_plant sim_plant_t; /* layout private to sim/plant; POD, memcpy-able */

/* Inputs sampled by the plant at each step. */
typedef struct sim_plant_inputs {
    int relay_cmd;      /* level the firmware drives on the relay pin: 1 = energise */
    int hx711_powered;  /* 0 while the firmware holds SCK high (> 60 us) for power-down */
} sim_plant_inputs_t;

/* Everything the runtime, the trace, the page and the tests may read. Units: g, g/s, s. */
typedef struct sim_plant_outputs {
    double t_s;                 /* plant time since plant_init */

    /* relay and motor */
    int relay_contact;          /* contact state after latency and stuck faults */
    double motor_speed;         /* 0..1 of nominal speed */
    int motor_powered;          /* contact closed and no stall fault */
    int motor_stalled;

    /* grounds path */
    double flow_burr_gps;       /* grounds leaving the burrs now */
    double flow_cup_gps;        /* grounds arriving at the cup (or platform) now */
    double m_loaded_g;          /* total beans ever loaded (conservation reference) */
    double m_hopper_g;          /* beans above the burrs, not yet feeding */
    double m_burr_g;            /* beans/grounds in the burr chamber */
    double m_chute_g;           /* grounds retained in the chute */
    double m_inflight_g;        /* grounds between chute exit and cup */
    double m_cup_g;             /* grounds inside the cup, wherever the cup is */
    double m_platform_g;        /* grounds that fell onto the bare platform (no cup present) */
    double m_spilled_g;         /* grounds discarded by the user (emptied cup, purge tipped out) */
    double conservation_error_g;/* m_loaded - sum of the above; must stay ~0 */

    /* cup and load cell */
    int cup_present;
    double cup_mass_g;          /* empty cup (vessel) mass */
    double scale_true_g;        /* static mass on the platform: cup+contents if present, platform grounds, press */
    double scale_signal_g;      /* sensed mass incl. impact, vibration, creep, drift (pre-ADC noise) */

    /* HX711 */
    int hx711_connected;        /* 0: module disconnected (DOUT floats to the ESP32 pull-down) */
    int hx711_powered;
    int hx711_ready;            /* a conversion is waiting to be clocked out (DOUT LOW) */
    int32_t hx711_code;         /* latched conversion, signed 24-bit */
    uint32_t hx711_seq;         /* increments per completed conversion */
    double hx711_sps;           /* configured output data rate (10 or 80) */
} sim_plant_outputs_t;

/* Parameter table entry. `source` is "file:line" of the upstream citation, or "placeholder"
 * (then documented in sim/ASSUMPTIONS.md with rationale and sweep range). */
typedef struct sim_param_info {
    const char* name;
    const char* unit;
    double default_value;
    double sweep_min;
    double sweep_max;
    const char* source;
    const char* description;
} sim_param_info_t;

/* User actions on the physical twin. `value` meaning per action. */
enum sim_plant_action {
    SIM_ACT_PLACE_CUP = 1,    /* value: empty cup mass g (<=0: parameter default) */
    SIM_ACT_REMOVE_CUP = 2,   /* lift the cup (and its contents) off the platform */
    SIM_ACT_EMPTY_CUP = 3,    /* tip the cup contents into m_spilled (cup must be off) */
    SIM_ACT_LOAD_BEANS = 4,   /* value: grams of beans dropped into the single-dose hopper */
    SIM_ACT_BUMP = 5,         /* value: peak transient force in g on the platform */
    SIM_ACT_PRESS = 6,        /* value: steady extra mass in g (a hand resting); 0 releases */
    SIM_ACT_CLEAN_CHUTE = 7,  /* move chute-retained grounds to m_spilled */
    SIM_ACT_WIPE_PLATFORM = 8 /* move grounds on the bare platform to m_spilled */
};

/* Faults. `active` 1/0; `value` meaning per fault. */
enum sim_plant_fault {
    SIM_FAULT_LC_DISCONNECT = 1,  /* HX711 module unplugged */
    SIM_FAULT_LC_STUCK = 2,       /* HX711 keeps returning the last code */
    SIM_FAULT_LC_NOISE_BURST = 3, /* value: noise multiplier (e.g. 20) */
    SIM_FAULT_RELAY_STUCK_ON = 4, /* contacts welded closed */
    SIM_FAULT_RELAY_STUCK_OFF = 5,/* contacts never close */
    SIM_FAULT_MOTOR_STALL = 6,    /* burrs jammed: speed -> 0 while powered */
    SIM_FAULT_FEED_BLOCK = 7      /* beans stop feeding from hopper into burrs (bridging) */
};

size_t plant_sizeof(void);
void plant_init(sim_plant_t* p, uint64_t seed);                 /* defaults, empty, no cup */
int plant_param_count(void);
const sim_param_info_t* plant_param_info(int index);
int plant_set_param(sim_plant_t* p, const char* name, double value); /* 0 ok, -1 unknown, -2 non-finite */
double plant_get_param(const sim_plant_t* p, const char* name, int* ok);
void plant_step(sim_plant_t* p, const sim_plant_inputs_t* in, double dt_s);
void plant_get_outputs(const sim_plant_t* p, sim_plant_outputs_t* out);
void plant_hx711_consume(sim_plant_t* p);                        /* conversion clocked out */
void plant_action(sim_plant_t* p, int action, double value);
void plant_fault(sim_plant_t* p, int fault, int active, double value);

/* ======================================================================================
 * 2. RUNTIME (host CLI and WASM exports)
 * ====================================================================================== */

/* Read-only snapshot for the dashboard, trace and tests. */
typedef struct sim_state {
    uint64_t t_us;
    sim_plant_outputs_t plant;

    int relay_pin_level;         /* level the firmware drives (GPIO or RMT) */
    int fw_phase;                /* GrindPhase value, -1 before the controller exists */
    int ui_state;                /* UIState value, -1 before boot */
    int fw_mode;                 /* GrindMode */
    float fw_weight_g;           /* WeightSensor::get_weight_low_latency */
    float fw_display_weight_g;   /* WeightSensor::get_display_weight */
    float fw_flow_gps;           /* WeightSensor::get_flow_rate (200 ms window) */
    float fw_target_g;           /* GrindController::get_target_weight */
    float fw_stop_offset_g;      /* GrindController::get_motor_stop_target_weight */
    float fw_latency_ms;         /* GrindController::get_grind_latency_ms */
    int fw_result;               /* GrindSessionResult of the last session */
    int fw_scale_fault;          /* WeightSensor::HardwareFault */
    int fw_safety_stop;          /* Grinder::has_safety_stop */
    int booted;                  /* setup() has returned */
    int fb_dirty;                /* framebuffer changed since last sim_framebuffer_take_dirty */
    uint32_t grinds_completed;   /* terminal sessions seen by the runtime */
} sim_state_t;

int sim_create(uint64_t seed);                 /* fresh world; returns 0 on success */
int sim_set_param(const char* name, double v); /* plant or runtime parameter by name */
int sim_load_params_json(const char* json);    /* {"name": value, ...} or {"name": {"value": v}} */
void sim_seed_setting_float(const char* ns, const char* key, float v); /* pre-boot NVS seed */
void sim_seed_setting_int(const char* ns, const char* key, int32_t v);
void sim_seed_setting_bool(const char* ns, const char* key, int v);
void sim_boot(void);                           /* start the firmware (setup() then loop()) */
void sim_run_ms(uint32_t ms);                  /* advance virtual time */
void sim_get_state(sim_state_t* out);
const char* sim_phase_name(int phase);
const char* sim_ui_state_name(int ui_state);

void sim_touch(int x, int y, int pressed);     /* virtual finger on the 280x456 panel */
const uint16_t* sim_framebuffer(void);         /* 280*456 RGB565, native byte order */
int sim_framebuffer_take_dirty(void);          /* 1 if changed since the last call */
float sim_display_brightness(void);            /* 0..1, as set by the firmware */
int sim_display_on(void);

void sim_action(int action, double value);     /* SIM_ACT_* */
void sim_fault(int fault, int active, double value); /* SIM_FAULT_* */

size_t sim_log_read(char* buf, size_t cap);    /* drain firmware log text */
void sim_trace_period_ms(uint32_t period_ms);  /* 0 disables */
size_t sim_trace_read(char* buf, size_t cap);  /* drain CSV rows (header first) */

/* Reset support: NVS + LittleFS + plant + clock, restored into a fresh firmware instance. */
size_t sim_persist_export(uint8_t* buf, size_t cap);
int sim_persist_import(const uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SIM_API_H */
