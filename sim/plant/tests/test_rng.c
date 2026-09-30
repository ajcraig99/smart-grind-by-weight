#include "test_util.h"

int main(void) {
    plant_rng_t a, b, c;
    uint64_t sm = 0;
    double sum = 0.0, sum2 = 0.0;
    int i;
    const int n = 200000;

    /* splitmix64 reference value for state 0: first output of the published generator
     * (check of the implementation; recalled from the reference implementation). */
    CHECK(plant_splitmix64(&sm) == UINT64_C(0xE220A8397B1DCDAF));

    plant_rng_seed(&a, 42);
    plant_rng_seed(&b, 42);
    plant_rng_seed(&c, 43);
    {
        int same_ab = 1, same_ac = 1;
        for (i = 0; i < 1000; i++) {
            const uint64_t x = plant_rng_next_u64(&a);
            const uint64_t y = plant_rng_next_u64(&b);
            const uint64_t z = plant_rng_next_u64(&c);
            if (x != y) same_ab = 0;
            if (x != z) same_ac = 0;
        }
        CHECK(same_ab);
        CHECK(!same_ac);
    }

    plant_rng_seed(&a, 0); /* seed 0 must not degenerate */
    CHECK((a.s[0] | a.s[1] | a.s[2] | a.s[3]) != 0);
    CHECK(plant_rng_next_u64(&a) != plant_rng_next_u64(&a));

    plant_rng_seed(&a, 7);
    for (i = 0; i < n; i++) {
        const double u = plant_rng_uniform(&a);
        if (!(u >= 0.0 && u < 1.0)) CHECK(0);
        sum += u;
        sum2 += u * u;
    }
    CHECK_NEAR(sum / n, 0.5, 0.005);
    CHECK_NEAR(sum2 / n - (sum / n) * (sum / n), 1.0 / 12.0, 0.002);

    sum = sum2 = 0.0;
    for (i = 0; i < n; i++) {
        const double g = plant_rng_gauss(&a);
        sum += g;
        sum2 += g * g;
    }
    CHECK_NEAR(sum / n, 0.0, 0.01);
    CHECK_NEAR(sum2 / n - (sum / n) * (sum / n), 1.0, 0.02);

    sum = 0.0;
    for (i = 0; i < n; i++) {
        const double e = plant_rng_exp1(&a);
        if (!(e >= 0.0)) CHECK(0);
        sum += e;
    }
    CHECK_NEAR(sum / n, 1.0, 0.02);

    sum = 0.0;
    for (i = 0; i < n; i++) {
        const double u = plant_rng_uniform_pm1(&a);
        if (!(u >= -1.0 && u < 1.0)) CHECK(0);
        sum += u;
    }
    CHECK_NEAR(sum / n, 0.0, 0.01);

    return t_finish("rng");
}
