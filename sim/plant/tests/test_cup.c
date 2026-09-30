#include "test_util.h"

int main(void) {
    struct sim_plant* p = t_new(1);
    sim_plant_outputs_t o;

    t_quiet(p);

    /* ---- no cup at start ---- */
    o = t_out(p);
    CHECK(o.cup_present == 0);
    CHECK_NEAR(o.scale_true_g, 0.0, 0.0);
    CHECK_NEAR(o.cup_mass_g, 100.0, 0.0); /* default reported until a cup is placed */

    /* ---- place: default mass, then a custom one on a fresh plant ---- */
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    o = t_out(p);
    CHECK(o.cup_present == 1);
    CHECK_NEAR(o.cup_mass_g, 100.0, 0.0);
    CHECK_NEAR(o.scale_true_g, 100.0, 0.0);
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 75.0);
    o = t_out(p);
    CHECK_NEAR(o.cup_mass_g, 75.0, 0.0);
    CHECK_NEAR(o.scale_true_g, 75.0, 0.0);
    plant_action(p, SIM_ACT_PLACE_CUP, 200.0); /* already on the platform: ignored */
    CHECK_NEAR(t_out(p).scale_true_g, 75.0, 0.0);

    /* ---- grounds land in the cup ---- */
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 1, 40.0);
    t_run(p, 0, 3.0);
    o = t_out(p);
    CHECK_NEAR(o.m_cup_g, 17.45, 3e-3);
    CHECK_NEAR(o.scale_true_g, 75.0 + o.m_cup_g, 1e-12);
    CHECK_NEAR(o.m_platform_g, 0.0, 0.0);

    /* ---- remove: contents go off-scale with the cup; the scale reads empty ---- */
    {
        const double in_cup = o.m_cup_g;
        plant_action(p, SIM_ACT_REMOVE_CUP, 0.0);
        o = t_out(p);
        CHECK(o.cup_present == 0);
        CHECK_NEAR(o.m_cup_g, in_cup, 0.0);
        CHECK_NEAR(o.scale_true_g, 0.0, 0.0);
        t_run(p, 0, 1.0);
        CHECK_NEAR(t_out(p).scale_signal_g, 0.0, 0.01);

        /* empty only works while the cup is off the platform */
        plant_action(p, SIM_ACT_EMPTY_CUP, 0.0);
        o = t_out(p);
        CHECK_NEAR(o.m_cup_g, 0.0, 0.0);
        CHECK_NEAR(o.m_spilled_g, in_cup, 1e-12);

        /* the same (now empty) cup returns with its own mass */
        plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
        o = t_out(p);
        CHECK(o.cup_present == 1);
        CHECK_NEAR(o.cup_mass_g, 75.0, 0.0);
        CHECK_NEAR(o.scale_true_g, 75.0, 1e-9);
    }

    /* ---- lifting and replacing keeps the contents ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 1, 40.0);
    t_run(p, 0, 3.0);
    {
        const double in_cup = t_out(p).m_cup_g;
        plant_action(p, SIM_ACT_REMOVE_CUP, 0.0);
        t_run(p, 0, 0.5);
        plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
        o = t_out(p);
        CHECK_NEAR(o.m_cup_g, in_cup, 0.0);
        CHECK_NEAR(o.scale_true_g, 100.0 + in_cup, 1e-12);
    }

    /* ---- grounds falling while the cup is off land on the platform and are wiped separately ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_LOAD_BEANS, 18.0);
    t_run(p, 1, 40.0);
    t_run(p, 0, 3.0);
    o = t_out(p);
    CHECK(o.m_platform_g > 16.0); /* 18 - 0.3 - 0.25 */
    CHECK_NEAR(o.scale_true_g, o.m_platform_g, 1e-12);
    {
        const double on_platform = o.m_platform_g;
        plant_action(p, SIM_ACT_WIPE_PLATFORM, 0.0);
        o = t_out(p);
        CHECK_NEAR(o.m_platform_g, 0.0, 0.0);
        CHECK_NEAR(o.m_spilled_g, on_platform, 1e-12);
        CHECK(fabs(o.conservation_error_g) < 1e-9);
    }

    /* ---- press: steady extra mass, released with 0, negative values ignored ---- */
    plant_init(p, 1);
    t_quiet(p);
    plant_action(p, SIM_ACT_PLACE_CUP, 0.0);
    plant_action(p, SIM_ACT_PRESS, 30.0);
    CHECK_NEAR(t_out(p).scale_true_g, 130.0, 0.0);
    t_run(p, 0, 1.0);
    CHECK_NEAR(t_out(p).scale_signal_g, 130.0, 0.01);
    plant_action(p, SIM_ACT_PRESS, 0.0);
    CHECK_NEAR(t_out(p).scale_true_g, 100.0, 0.0);
    plant_action(p, SIM_ACT_PRESS, -5.0);
    CHECK_NEAR(t_out(p).scale_true_g, 100.0, 0.0);
    /* a hand resting on the bare platform also weighs */
    plant_action(p, SIM_ACT_REMOVE_CUP, 0.0);
    plant_action(p, SIM_ACT_PRESS, 40.0);
    CHECK_NEAR(t_out(p).scale_true_g, 40.0, 0.0);

    /* ---- invalid actions and values are harmless ---- */
    plant_action(p, 99, 1.0);
    plant_action(p, SIM_ACT_LOAD_BEANS, -5.0);
    CHECK_NEAR(t_out(p).m_loaded_g, 0.0, 0.0);
    plant_fault(p, 99, 1, 1.0);

    free(p);
    return t_finish("cup");
}
