#include "rng.h"

#include <math.h>

#define PLANT_TWO_PI 6.283185307179586476925286766559

static uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t plant_splitmix64(uint64_t* state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

void plant_rng_seed(plant_rng_t* r, uint64_t seed) {
    uint64_t sm = seed;
    r->s[0] = plant_splitmix64(&sm);
    r->s[1] = plant_splitmix64(&sm);
    r->s[2] = plant_splitmix64(&sm);
    r->s[3] = plant_splitmix64(&sm);
    /* splitmix64 never yields four zero words in a row, but keep the invariant explicit. */
    if ((r->s[0] | r->s[1] | r->s[2] | r->s[3]) == 0) {
        r->s[0] = 1;
    }
}

uint64_t plant_rng_next_u64(plant_rng_t* r) {
    /* xoshiro256** 1.0 (public domain, Blackman and Vigna). */
    const uint64_t result = rotl64(r->s[1] * 5, 7) * 9;
    const uint64_t t = r->s[1] << 17;
    r->s[2] ^= r->s[0];
    r->s[3] ^= r->s[1];
    r->s[1] ^= r->s[2];
    r->s[0] ^= r->s[3];
    r->s[2] ^= t;
    r->s[3] = rotl64(r->s[3], 45);
    return result;
}

double plant_rng_uniform(plant_rng_t* r) {
    return (double)(plant_rng_next_u64(r) >> 11) * (1.0 / 9007199254740992.0);
}

double plant_rng_uniform_pm1(plant_rng_t* r) {
    return 2.0 * plant_rng_uniform(r) - 1.0;
}

double plant_rng_gauss(plant_rng_t* r) {
    const double u1 = 1.0 - plant_rng_uniform(r); /* (0, 1], log is finite */
    const double u2 = plant_rng_uniform(r);
    return sqrt(-2.0 * log(u1)) * cos(PLANT_TWO_PI * u2);
}

double plant_rng_exp1(plant_rng_t* r) {
    return -log(1.0 - plant_rng_uniform(r)); /* 1 - u in (0, 1] */
}
