/* plant_params.c - the parameter table, generated from plant_params.h. */
#include "plant.h"

#include <math.h>
#include <string.h>

static const sim_param_info_t k_params[PLANT_PARAM_COUNT] = {
#define PLANT_X_ROW(id, name, unit, def, mn, mx, src, desc) {name, unit, def, mn, mx, src, desc},
    PLANT_PARAM_LIST(PLANT_X_ROW)
#undef PLANT_X_ROW
};

int plant_param_count(void) {
    return PLANT_PARAM_COUNT;
}

const sim_param_info_t* plant_param_info(int index) {
    if (index < 0 || index >= PLANT_PARAM_COUNT) {
        return NULL;
    }
    return &k_params[index];
}

int plant_param_index(const char* name) {
    int i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < PLANT_PARAM_COUNT; i++) {
        if (strcmp(k_params[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void plant_params_load_defaults(double* values) {
    int i;
    for (i = 0; i < PLANT_PARAM_COUNT; i++) {
        values[i] = k_params[i].default_value;
    }
}

int plant_set_param(sim_plant_t* p, const char* name, double value) {
    const int idx = plant_param_index(name);
    if (p == NULL || idx < 0) {
        return -1;
    }
    if (!isfinite(value)) {
        return -2; /* known name, unusable value; nothing changed */
    }
    if (idx == P_HX711_SPS) {
        if (value < 1.0) value = 1.0;
        if (value > 1000.0) value = 1000.0;
    }
    p->p[idx] = value;
    return 0;
}

double plant_get_param(const sim_plant_t* p, const char* name, int* ok) {
    const int idx = plant_param_index(name);
    if (p == NULL || idx < 0) {
        if (ok != NULL) *ok = 0;
        return 0.0;
    }
    if (ok != NULL) *ok = 1;
    return p->p[idx];
}
