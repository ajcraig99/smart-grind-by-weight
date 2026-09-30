/*
 * Parameter table checks.
 *   argv[1]: path to ASSUMPTIONS_PLANT.md (every "placeholder" parameter must be listed there)
 *   argv[2]: path to plant.h (the state struct must not contain pointers)
 *   argv[3]: optional repository root; when given, every cited "path:line" source is verified
 *            against the upstream file (line exists, names the expected macro, value matches)
 */
#include "test_util.h"

#include <ctype.h>

static char* read_file(const char* path, size_t* len_out) {
    FILE* f = fopen(path, "rb");
    char* buf;
    long n;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char*)malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    if (len_out) *len_out = (size_t)n;
    return buf;
}

/* Expected upstream token for each cited parameter, and how to turn the cited number into the default. */
struct cite_check {
    const char* param;
    const char* token;
    double scale; /* default = parsed * scale + offset */
    double offset;
};

static const struct cite_check k_cites[] = {
    {"flow_nominal_gps", "DEBUG_MOCK_FLOW_RATE_GPS", 1.0, 0.0},
    {"lc_counts_per_g", "USER_DEFAULT_CALIBRATION_FACTOR", 1.0, 0.0},
    {"lc_baseline_code", "DEBUG_MOCK_BASELINE_RAW", 1.0, -8388608.0}, /* offset binary -> signed */
    {"lc_idle_noise_counts", "DEBUG_MOCK_IDLE_NOISE_RAW", 1.0, 0.0},
    {"lc_vibration_noise_counts", "DEBUG_MOCK_GRIND_NOISE_RAW", 1.0, 0.0},
    {"hx711_sps", "HW_LOADCELL_SAMPLE_RATE_SPS", 1.0, 0.0},
};

static void verify_citation(const sim_param_info_t* info, const char* root) {
    char path[1024], file[768];
    const char* colon = strrchr(info->source, ':');
    int line_no = 0, cur = 1;
    char* text;
    char* line;
    char* end;
    size_t k;
    const struct cite_check* cc = NULL;
    if (colon == NULL) return;
    snprintf(file, sizeof(file), "%.*s", (int)(colon - info->source), info->source);
    line_no = atoi(colon + 1);
    snprintf(path, sizeof(path), "%s/%s", root, file);
    text = read_file(path, NULL);
    if (text == NULL) {
        printf("FAIL: cited file %s for %s not found\n", path, info->name);
        t_fail++;
        return;
    }
    line = text;
    while (cur < line_no && line != NULL) {
        line = strchr(line, '\n');
        if (line != NULL) line++;
        cur++;
    }
    if (line == NULL) {
        printf("FAIL: %s has no line %d (cited by %s)\n", file, line_no, info->name);
        t_fail++;
        free(text);
        return;
    }
    end = strchr(line, '\n');
    if (end != NULL) *end = '\0';
    for (k = 0; k < sizeof(k_cites) / sizeof(k_cites[0]); k++) {
        if (strcmp(k_cites[k].param, info->name) == 0) cc = &k_cites[k];
    }
    if (cc == NULL) {
        printf("FAIL: no citation check for %s\n", info->name);
        t_fail++;
    } else {
        const char* tok = strstr(line, cc->token);
        t_checks++;
        if (tok == NULL) {
            printf("FAIL: %s:%d does not contain %s (cited by %s)\n", file, line_no, cc->token, info->name);
            t_fail++;
        } else {
            const char* q = tok + strlen(cc->token);
            double v;
            while (*q != '\0' && !isdigit((unsigned char)*q) && *q != '-') q++;
            v = strtod(q, NULL);
            t_checks++;
            if (fabs(v * cc->scale + cc->offset - info->default_value) > 1e-9) {
                printf("FAIL: %s:%d gives %.9g, parameter %s default is %.9g\n", file, line_no,
                       v * cc->scale + cc->offset, info->name, info->default_value);
                t_fail++;
            }
        }
    }
    free(text);
}

static int is_path_line(const char* s) {
    const char* colon = strrchr(s, ':');
    const char* q;
    if (colon == NULL || colon == s || colon[1] == '\0') return 0;
    for (q = colon + 1; *q != '\0'; q++) {
        if (!isdigit((unsigned char)*q)) return 0;
    }
    return strchr(s, '/') != NULL;
}

int main(int argc, char** argv) {
    const int n = plant_param_count();
    struct sim_plant* p = t_new(1);
    char* assumptions = NULL;
    char* header = NULL;
    int i, j;

    CHECK(n == PLANT_PARAM_COUNT);
    CHECK(n >= 35);
    CHECK(plant_param_info(-1) == NULL);
    CHECK(plant_param_info(n) == NULL);
    CHECK(plant_sizeof() == sizeof(struct sim_plant));

    if (argc > 1) {
        assumptions = read_file(argv[1], NULL);
        CHECK(assumptions != NULL);
    }

    for (i = 0; i < n; i++) {
        const sim_param_info_t* info = plant_param_info(i);
        int ok = 0;
        CHECK(info != NULL && info->name != NULL && info->name[0] != '\0');
        CHECK(info->unit != NULL && info->description != NULL && info->description[0] != '\0');
        CHECK(info->sweep_min <= info->default_value && info->default_value <= info->sweep_max);
        CHECK(info->sweep_min < info->sweep_max);
        for (j = 0; j < i; j++) CHECK(strcmp(info->name, plant_param_info(j)->name) != 0);
        CHECK(plant_param_index(info->name) == i);
        CHECK_NEAR(plant_get_param(p, info->name, &ok), info->default_value, 0.0);
        CHECK(ok == 1);

        /* set/get round trip at the sweep ends (hx711_sps is clamped to 1..1000 only) */
        CHECK(plant_set_param(p, info->name, info->sweep_max) == 0);
        CHECK_NEAR(plant_get_param(p, info->name, &ok), info->sweep_max, 0.0);
        CHECK(plant_set_param(p, info->name, info->sweep_min) == 0);
        CHECK_NEAR(plant_get_param(p, info->name, &ok), info->sweep_min, 0.0);
        CHECK(plant_set_param(p, info->name, info->default_value) == 0);

        /* source: exact path:line citation or the literal "placeholder" */
        CHECK(info->source != NULL);
        if (strcmp(info->source, "placeholder") == 0) {
            if (assumptions != NULL && strstr(assumptions, info->name) == NULL) {
                printf("FAIL: placeholder %s is not listed in ASSUMPTIONS_PLANT.md\n", info->name);
                t_fail++;
            }
        } else {
            CHECK(is_path_line(info->source));
            if (argc > 3) verify_citation(info, argv[3]);
        }
    }

    /* unknown names, bad values */
    {
        int ok = 1;
        CHECK(plant_set_param(p, "no_such_parameter", 1.0) == -1);
        CHECK(plant_set_param(p, NULL, 1.0) == -1);
        (void)plant_get_param(p, "no_such_parameter", &ok);
        CHECK(ok == 0);
        CHECK(plant_set_param(p, "flow_nominal_gps", NAN) == -2);
        CHECK(plant_set_param(p, "flow_nominal_gps", INFINITY) == -2);
        CHECK_NEAR(plant_get_param(p, "flow_nominal_gps", &ok), 1.9, 0.0);
        CHECK(plant_get_param(p, "flow_nominal_gps", NULL) == 1.9);
    }

    /* parameters the brief requires */
    {
        const char* required[] = {
            "relay_on_latency_ms", "relay_off_latency_ms", "motor_tau_up_s", "motor_tau_down_s",
            "motor_min_speed", "flow_nominal_gps", "grind_setting_factor", "bean_factor",
            "flow_noise_frac", "flow_noise_tau_s", "clump_rate_hz", "clump_mass_g",
            "burr_taper_mass_g", "burr_residual_g", "burr_capacity_g", "feed_rate_gps",
            "chute_retention_g", "chute_tau_s", "transport_delay_s", "drop_height_m", "cup_mass_g",
            "lc_counts_per_g", "lc_baseline_code", "lc_gain_error", "lc_idle_noise_counts",
            "lc_vibration_noise_counts", "lc_natural_freq_hz", "lc_damping", "lc_creep_frac",
            "lc_creep_tau_s", "lc_drift_g_per_min", "hx711_sps", "hx711_settle_ms_10sps",
            "hx711_settle_ms_80sps"};
        for (i = 0; i < (int)(sizeof(required) / sizeof(required[0])); i++) {
            CHECK(plant_param_index(required[i]) >= 0);
        }
    }

    /* the cited defaults */
    {
        int ok;
        CHECK_NEAR(plant_get_param(p, "flow_nominal_gps", &ok), 1.9, 0.0);
        CHECK_NEAR(plant_get_param(p, "lc_counts_per_g", &ok), -7050.0, 0.0);
        CHECK_NEAR(plant_get_param(p, "lc_baseline_code", &ok), -1048576.0, 0.0);
        CHECK_NEAR(plant_get_param(p, "lc_idle_noise_counts", &ok), 60.0, 0.0);
        CHECK_NEAR(plant_get_param(p, "lc_vibration_noise_counts", &ok), 400.0, 0.0);
        CHECK_NEAR(plant_get_param(p, "hx711_sps", &ok), 10.0, 0.0);
    }

    /* the state struct holds no pointers: scan the declaration in plant.h (comments stripped) */
    if (argc > 2) {
        header = read_file(argv[2], NULL);
        CHECK(header != NULL);
        if (header != NULL) {
            char* start = strstr(header, "struct sim_plant {");
            char* stop = start ? strstr(start, "\n};") : NULL;
            CHECK(start != NULL && stop != NULL);
            if (start != NULL && stop != NULL) {
                char* q;
                int in_comment = 0, stars = 0;
                *stop = '\0';
                for (q = start; *q != '\0'; q++) {
                    if (!in_comment && q[0] == '/' && q[1] == '*') { in_comment = 1; q++; continue; }
                    if (in_comment && q[0] == '*' && q[1] == '/') { in_comment = 0; q++; continue; }
                    if (!in_comment && *q == '*') stars++;
                }
                CHECK(stars == 0);
            }
        }
    }

    free(header);
    free(assumptions);
    free(p);
    return t_finish("params");
}
