/*
 * rng.h - seedable pseudo-random generator for the plant model.
 *
 * splitmix64 expands the 64-bit seed into the state of a xoshiro256** generator.
 * The state is four plain uint64_t, so it is memcpy-able inside struct sim_plant.
 * Never use rand()/random(): every random draw in the plant comes from here so that the
 * same seed reproduces the same run byte for byte.
 */
#ifndef PLANT_RNG_H
#define PLANT_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct plant_rng {
    uint64_t s[4];
} plant_rng_t;

uint64_t plant_splitmix64(uint64_t* state);        /* advances *state, returns next output */
void plant_rng_seed(plant_rng_t* r, uint64_t seed);
uint64_t plant_rng_next_u64(plant_rng_t* r);
double plant_rng_uniform(plant_rng_t* r);          /* [0, 1) with 53 random bits */
double plant_rng_uniform_pm1(plant_rng_t* r);      /* [-1, 1) */
double plant_rng_gauss(plant_rng_t* r);            /* N(0,1), Box-Muller, no cached spare */
double plant_rng_exp1(plant_rng_t* r);             /* exponential, mean 1 */

#ifdef __cplusplus
}
#endif

#endif /* PLANT_RNG_H */
