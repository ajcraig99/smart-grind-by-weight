/*
 * plant_paramgen - writes params_default.json, params.schema.json and PARAMS.md from the C
 * parameter table (the single source of truth).
 *   usage: plant_paramgen <params_default.json> <params.schema.json> <PARAMS.md>
 * CTest regenerates the three files in the build tree and compares them with the committed copies.
 */
#include <stdio.h>
#include <string.h>

#include "plant.h"

static void json_string(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s != '\0'; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void json_number(FILE* f, double v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.10g", v);
    fputs(buf, f);
}

static void write_defaults(FILE* f) {
    const int n = plant_param_count();
    int i;
    fputs("{\n", f);
    for (i = 0; i < n; i++) {
        const sim_param_info_t* p = plant_param_info(i);
        fputs("  ", f);
        json_string(f, p->name);
        fputs(": {\"value\": ", f);
        json_number(f, p->default_value);
        fputs(", \"min\": ", f);
        json_number(f, p->sweep_min);
        fputs(", \"max\": ", f);
        json_number(f, p->sweep_max);
        fputs(", \"unit\": ", f);
        json_string(f, p->unit);
        fputs(", \"source\": ", f);
        json_string(f, p->source);
        fputs(", \"description\": ", f);
        json_string(f, p->description);
        fputs(i + 1 < n ? "},\n" : "}\n", f);
    }
    fputs("}\n", f);
}

static void write_schema(FILE* f) {
    const int n = plant_param_count();
    int i;
    fputs("{\n  \"$schema\": \"http://json-schema.org/draft-07/schema#\",\n", f);
    fputs("  \"title\": \"Digital twin plant parameters\",\n", f);
    fputs("  \"description\": \"Generated from sim/plant/plant_params.h by plant_paramgen. Each parameter is either a bare number or an object with at least value. x-default and the x-sweep bounds give the Monte Carlo sweep range; values outside the range are allowed for experiments.\",\n", f);
    fputs("  \"type\": \"object\",\n  \"additionalProperties\": false,\n  \"properties\": {\n", f);
    for (i = 0; i < n; i++) {
        const sim_param_info_t* p = plant_param_info(i);
        fputs("    ", f);
        json_string(f, p->name);
        fputs(": {\n      \"description\": ", f);
        json_string(f, p->description);
        fputs(",\n      \"x-default\": ", f);
        json_number(f, p->default_value);
        fputs(",\n      \"x-sweep-min\": ", f);
        json_number(f, p->sweep_min);
        fputs(",\n      \"x-sweep-max\": ", f);
        json_number(f, p->sweep_max);
        fputs(",\n      \"x-unit\": ", f);
        json_string(f, p->unit);
        fputs(",\n      \"x-source\": ", f);
        json_string(f, p->source);
        fputs(",\n      \"anyOf\": [\n        {\"type\": \"number\"},\n        {\n          \"type\": \"object\",\n", f);
        fputs("          \"required\": [\"value\"],\n          \"additionalProperties\": false,\n", f);
        fputs("          \"properties\": {\n            \"value\": {\"type\": \"number\"},\n", f);
        fputs("            \"min\": {\"type\": \"number\"},\n            \"max\": {\"type\": \"number\"},\n", f);
        fputs("            \"unit\": {\"type\": \"string\", \"const\": ", f);
        json_string(f, p->unit);
        fputs("},\n            \"source\": {\"type\": \"string\"},\n            \"description\": {\"type\": \"string\"}\n          }\n        }\n      ]\n    }", f);
        fputs(i + 1 < n ? ",\n" : "\n", f);
    }
    fputs("  }\n}\n", f);
}

static void write_markdown(FILE* f) {
    const int n = plant_param_count();
    int i;
    fputs("# Plant parameters\n\n", f);
    fputs("Generated from `plant_params.h` by `plant_paramgen` (do not edit; CTest fails if this file is out of date).\n", f);
    fputs("`source` is an exact `path:line` citation of the upstream firmware repository (verified by `tests/test_params.c`), or\n", f);
    fputs("`placeholder`, in which case the rationale and sweep range are in `ASSUMPTIONS_PLANT.md`.\n\n", f);
    fputs("| Name | Unit | Default | Sweep min | Sweep max | Source | Description |\n", f);
    fputs("|------|------|---------|-----------|-----------|--------|-------------|\n", f);
    for (i = 0; i < n; i++) {
        const sim_param_info_t* p = plant_param_info(i);
        fprintf(f, "| `%s` | %s | ", p->name, p->unit);
        json_number(f, p->default_value);
        fputs(" | ", f);
        json_number(f, p->sweep_min);
        fputs(" | ", f);
        json_number(f, p->sweep_max);
        fprintf(f, " | %s | %s |\n", strcmp(p->source, "placeholder") == 0 ? "placeholder" : p->source, p->description);
    }
}

int main(int argc, char** argv) {
    FILE* f;
    if (argc != 4) {
        fprintf(stderr, "usage: %s params_default.json params.schema.json PARAMS.md\n", argv[0]);
        return 2;
    }
    f = fopen(argv[1], "wb");
    if (f == NULL) return 1;
    write_defaults(f);
    fclose(f);
    f = fopen(argv[2], "wb");
    if (f == NULL) return 1;
    write_schema(f);
    fclose(f);
    f = fopen(argv[3], "wb");
    if (f == NULL) return 1;
    write_markdown(f);
    fclose(f);
    return 0;
}
