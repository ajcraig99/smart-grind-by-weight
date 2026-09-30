/*
 * Determinism, memcpy save/restore and the no-pointers property of the plant state.
 *
 * The scripted scenario is a pure function of the step index (it does not use the plant's RNG), so
 * it can be resumed from any step after a restore.
 */
#include "test_util.h"

#define FNV_OFFSET UINT64_C(1469598103934665603)
#define FNV_PRIME UINT64_C(1099511628211)

static uint64_t fnv(uint64_t h, const void* data, size_t n) {
    const unsigned char* b = (const unsigned char*)data;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= b[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* Stateless pseudo-random decision for step index i. */
static uint64_t decide(uint64_t i, uint64_t salt) {
    uint64_t s = i * UINT64_C(0x9E3779B97F4A7C15) + salt;
    return plant_splitmix64(&s);
}

static void scripted_step(struct sim_plant* p, uint64_t i, uint64_t* hash) {
    sim_plant_inputs_t in;
    sim_plant_outputs_t o;
    const uint64_t sec = i / 1000;
    /* fixed timeline (seconds) */
    if (i % 1000 == 0) {
        if (sec == 1) plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
        if (sec == 2) plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
        if (sec == 12) plant_action(p, SIM_ACT_BUMP, 30.0);
        if (sec == 20) plant_fault(p, SIM_FAULT_LC_NOISE_BURST, 1, 10.0);
        if (sec == 23) plant_fault(p, SIM_FAULT_LC_NOISE_BURST, 0, 0.0);
        if (sec == 30) plant_action(p, SIM_ACT_REMOVE_CUP, 0.0);
        if (sec == 32) plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
        if (sec == 35) plant_action(p, SIM_ACT_LOAD_BEANS, 10.0);
        if (sec == 40) plant_fault(p, SIM_FAULT_MOTOR_STALL, 1, 0.0);
        if (sec == 41) plant_fault(p, SIM_FAULT_MOTOR_STALL, 0, 0.0);
        if (sec == 50) plant_fault(p, SIM_FAULT_LC_STUCK, 1, 0.0);
        if (sec == 52) plant_fault(p, SIM_FAULT_LC_STUCK, 0, 0.0);
        if (sec == 55) plant_action(p, SIM_ACT_PRESS, 20.0);
    }
    in.relay_cmd = ((sec >= 3 && sec < 15) || (sec >= 36 && sec < 48) || (decide(i / 250, 7) & 3u) == 0) ? 1 : 0;
    in.hx711_powered = (sec >= 57 && sec < 58) ? 0 : 1;
    plant_step(p, &in, SIM_PLANT_DT_S);
    plant_get_outputs(p, &o);
    if (o.hx711_ready && (decide(i, 99) & 1u)) plant_hx711_consume(p);
    *hash = fnv(*hash, &o, sizeof(o));
}

static uint64_t run_range(struct sim_plant* p, uint64_t from, uint64_t to) {
    uint64_t h = FNV_OFFSET, i;
    for (i = from; i < to; i++) scripted_step(p, i, &h);
    return h;
}

int main(void) {
    struct sim_plant* a = t_new(1234);
    struct sim_plant* b = t_new(1234);
    struct sim_plant* c = t_new(1235);
    struct sim_plant* d = (struct sim_plant*)malloc(sizeof(struct sim_plant));
    uint64_t ha, hb;
    int32_t codes_a[600], codes_c[600];
    int i, differing = 0;

    CHECK(plant_sizeof() == sizeof(struct sim_plant));

    /* same seed, same inputs: identical outputs byte for byte over 60 s, identical state */
    ha = run_range(a, 0, 60000);
    hb = run_range(b, 0, 60000);
    CHECK(ha == hb);
    CHECK(memcmp(a, b, sizeof(struct sim_plant)) == 0);

    /* a different seed gives a different noise realisation (same script) */
    plant_init(a, 1234);
    plant_init(c, 1235);
    for (i = 0; i < 600; i++) {
        uint64_t h1 = FNV_OFFSET, h2 = FNV_OFFSET, k;
        for (k = (uint64_t)i * 100; k < (uint64_t)(i + 1) * 100; k++) {
            scripted_step(a, k, &h1);
            scripted_step(c, k, &h2);
        }
        codes_a[i] = t_out(a).hx711_code;
        codes_c[i] = t_out(c).hx711_code;
        if (codes_a[i] != codes_c[i]) differing++;
        (void)h1;
        (void)h2;
    }
    CHECK(differing > 300);

    /* memcpy save/restore mid-run continues identically (into a different object) */
    plant_init(a, 77);
    (void)run_range(a, 0, 20000);
    memcpy(d, a, sizeof(struct sim_plant));
    ha = run_range(a, 20000, 60000);
    hb = run_range(d, 20000, 60000);
    CHECK(ha == hb);
    CHECK(memcmp(a, d, sizeof(struct sim_plant)) == 0);

    /* and equals an uninterrupted run */
    plant_init(b, 77);
    hb = run_range(b, 0, 20000);
    hb = run_range(b, 20000, 60000);
    CHECK(hb == ha);

    /* restoring an older snapshot over a newer plant rewinds it exactly */
    plant_init(a, 5);
    (void)run_range(a, 0, 5000);
    memcpy(d, a, sizeof(struct sim_plant));
    ha = run_range(a, 5000, 15000);
    (void)run_range(a, 15000, 25000);
    memcpy(a, d, sizeof(struct sim_plant));
    hb = run_range(a, 5000, 15000);
    CHECK(ha == hb);

    free(a);
    free(b);
    free(c);
    free(d);
    return t_finish("determinism");
}
