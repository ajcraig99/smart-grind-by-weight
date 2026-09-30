#include "test_util.h"

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;
    double e1, e2;

    /* ---- grounds below the retention capacity stay in the chute ---- */
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 0, 1.0);
    t_steps(p, 1, 1, 120, SIM_PLANT_DT_S); /* one short pulse, about 0.17 g < 0.25 g retention */
    t_run(p, 0, 3.0);
    o = t_out(p);
    CHECK(o.m_chute_g > 0.1 && o.m_chute_g < 0.25);
    CHECK_NEAR(o.m_cup_g, 0.0, 1e-12);
    CHECK_NEAR(o.m_inflight_g, 0.0, 1e-12);
    CHECK_NEAR(o.scale_true_g, 100.0, 1e-9); /* nothing reached the scale */
    {
        const double held = o.m_chute_g;
        plant_action(p, SIM_ACT_CLEAN_CHUTE, 0.0);
        o = t_out(p);
        CHECK_NEAR(o.m_chute_g, 0.0, 0.0);
        CHECK_NEAR(o.m_spilled_g, held, 1e-12);
        CHECK(fabs(o.conservation_error_g) < 1e-12);
    }

    /* ---- continuous grinding: steady store = retention + flow * drain time ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 1, 3.0);
    o = t_out(p);
    {
        /* discrete steady state: excess * (1 - exp(-dt/tau)) = flow * dt */
        const double alpha = 1.0 - exp(-0.001 / 0.06);
        const double expected = 0.25 + 1.9 * 0.001 / alpha;
        CHECK_NEAR(o.m_chute_g, expected, 2e-3);
        CHECK_NEAR(o.flow_cup_gps, 1.9, 2e-3); /* what goes in comes out */
    }

    /* ---- after the burrs stop, the excess drains first order; the retention stays ---- */
    t_steps(p, 0, 1, 300, SIM_PLANT_DT_S);
    e1 = t_out(p).m_chute_g - 0.25;
    t_steps(p, 0, 1, 60, SIM_PLANT_DT_S); /* one chute_tau_s later */
    e2 = t_out(p).m_chute_g - 0.25;
    CHECK(e1 > 1e-3);
    CHECK_NEAR(e2 / e1, exp(-1.0), 0.02);
    t_run(p, 0, 3.0);
    o = t_out(p);
    CHECK_NEAR(o.m_chute_g, 0.25, 1e-9);
    CHECK_NEAR(o.m_cup_g, 30.0 - o.m_hopper_g - o.m_burr_g - 0.25, 1e-9);

    /* ---- zero retention: the chute empties completely ---- */
    plant_init(p, 1);
    t_quiet(p);
    t_set(p, "chute_retention_g", 0.0);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 30.0);
    t_run(p, 1, 2.0);
    t_run(p, 0, 3.0);
    CHECK_NEAR(t_out(p).m_chute_g, 0.0, 1e-9);

    /* ---- a second dose passes through a primed chute without losing its retention again ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 1, 40.0);
    t_run(p, 0, 2.0);
    {
        const double first = t_out(p).m_cup_g; /* 18 - residual - retention */
        plant_action(p, SIM_ACT_EMPTY_CUP, 0.0); /* ignored, cup still on the platform */
        CHECK_NEAR(t_out(p).m_cup_g, first, 0.0);
        plant_action(p, SIM_ACT_REMOVE_CUP, 0.0);
        plant_action(p, SIM_ACT_EMPTY_CUP, 0.0);
        plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
        plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
        t_run(p, 1, 40.0);
        t_run(p, 0, 2.0);
        o = t_out(p);
        /* chute still holds its 0.25 g and the chamber keeps its 0.3 g residual, so the second dose
         * delivers 18 g + the previous residual - the same residual: 18 - 0 */
        CHECK_NEAR(o.m_cup_g, 18.0, 5e-3);
        CHECK(fabs(o.conservation_error_g) < 1e-9);
    }

    free(p);
    return t_finish("chute");
}
