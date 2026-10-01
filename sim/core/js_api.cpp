// String-friendly entry points for the browser (exported from WASM) and usable natively.
// The page only calls these; all controller logic stays in the firmware.
#include "json.h"
#include "runtime.h"
#include "scheduler.h"
#include "world.h"
#include "layout_audit.h"
#include "ui_probe.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TWIN_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define TWIN_EXPORT extern "C"
#endif

namespace {
std::string g_out;
std::string g_scenario = "{\"name\":\"browser\",\"beans_g\":22.0}";
bool g_operator_running = false;

void add(std::string& s, const char* key, double v, int prec = 4) {
    char buf[96];
    if (!std::isfinite(v)) std::snprintf(buf, sizeof(buf), "\"%s\":null,", key);
    else std::snprintf(buf, sizeof(buf), "\"%s\":%.*f,", key, prec, v);
    s += buf;
}
void addi(std::string& s, const char* key, long long v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "\"%s\":%lld,", key, v);
    s += buf;
}
void adds(std::string& s, const char* key, const std::string& v) {
    s += "\"";
    s += key;
    s += "\":\"";
    for (char c : v) {
        if (c == '"' || c == '\\') s += '\\';
        if (c == '\n') { s += "\\n"; continue; }
        s += c;
    }
    s += "\",";
}
}  // namespace

// Fresh world with the default operator scenario's preferences (target, calibration flag).
TWIN_EXPORT int twin_init(double seed, double target_g) {
    sim_create(static_cast<uint64_t>(seed));
    std::string err;
    sim::operator_load_scenario_json(g_scenario, &err);
    if (target_g > 0) sim::operator_set_target(static_cast<float>(target_g));
    sim::operator_seed_preferences();
    sim_trace_period_ms(10);
    g_operator_running = false;
    return 0;
}

TWIN_EXPORT int twin_set_scenario(const char* json) {
    std::string err;
    if (!json || !sim::operator_load_scenario_json(json, &err)) return -1;
    g_scenario = json;
    return 0;
}

TWIN_EXPORT void twin_boot() { sim_boot(); }
TWIN_EXPORT void twin_run_ms(double ms) {
    if (ms <= 0) return;
    sim_run_ms(static_cast<uint32_t>(ms + 0.5));
}
TWIN_EXPORT int twin_set_param(const char* name, double v) { return sim_set_param(name, v); }
TWIN_EXPORT double twin_get_param(const char* name) {
    int ok = 0;
    const double v = plant_get_param(sim::plant(), name, &ok);
    return ok ? v : NAN;
}
TWIN_EXPORT void twin_action(int action, double value) { sim_action(action, value); }
TWIN_EXPORT void twin_fault(int fault, int active, double value) { sim_fault(fault, active, value); }
TWIN_EXPORT void twin_touch(int x, int y, int pressed) { sim_touch(x, y, pressed); }
TWIN_EXPORT uint16_t* twin_framebuffer() { return const_cast<uint16_t*>(sim_framebuffer()); }
TWIN_EXPORT int twin_framebuffer_dirty() { return sim_framebuffer_take_dirty(); }
TWIN_EXPORT void twin_operator_start() {
    sim::operator_start();
    g_operator_running = true;
}
TWIN_EXPORT int twin_restart_requested() { return sim::world_restart_requested() ? 1 : 0; }

TWIN_EXPORT const char* twin_param_table() {
    g_out = "[";
    for (int i = 0; i < plant_param_count(); ++i) {
        const sim_param_info_t* p = plant_param_info(i);
        std::string o = "{";
        adds(o, "name", p->name);
        adds(o, "unit", p->unit ? p->unit : "");
        add(o, "default", p->default_value, 6);
        add(o, "min", p->sweep_min, 6);
        add(o, "max", p->sweep_max, 6);
        adds(o, "source", p->source ? p->source : "");
        adds(o, "description", p->description ? p->description : "");
        o.back() = '}';
        g_out += o;
        if (i + 1 < plant_param_count()) g_out += ",";
    }
    g_out += "]";
    return g_out.c_str();
}

TWIN_EXPORT const char* twin_state_json() {
    sim_state_t st;
    sim_get_state(&st);
    const sim_plant_outputs_t& o = st.plant;
    std::string s = "{";
    add(s, "t_s", static_cast<double>(st.t_us) / 1e6, 3);
    addi(s, "phase", st.fw_phase);
    adds(s, "phase_name", sim_phase_name(st.fw_phase));
    addi(s, "ui_state", st.ui_state);
    adds(s, "ui_state_name", sim_ui_state_name(st.ui_state));
    addi(s, "mode", st.fw_mode);
    addi(s, "booted", st.booted);
    addi(s, "relay_pin", st.relay_pin_level);
    addi(s, "relay_contact", o.relay_contact);
    add(s, "motor_speed", o.motor_speed);
    addi(s, "motor_stalled", o.motor_stalled);
    add(s, "flow_burr_gps", o.flow_burr_gps);
    add(s, "flow_cup_gps", o.flow_cup_gps);
    add(s, "m_loaded_g", o.m_loaded_g);
    add(s, "m_hopper_g", o.m_hopper_g);
    add(s, "m_burr_g", o.m_burr_g);
    add(s, "m_chute_g", o.m_chute_g);
    add(s, "m_inflight_g", o.m_inflight_g);
    add(s, "m_cup_g", o.m_cup_g);
    add(s, "m_platform_g", o.m_platform_g);
    add(s, "m_spilled_g", o.m_spilled_g);
    add(s, "conservation_error_g", o.conservation_error_g, 9);
    addi(s, "cup_present", o.cup_present);
    add(s, "cup_mass_g", o.cup_mass_g);
    add(s, "scale_true_g", o.scale_true_g);
    add(s, "scale_signal_g", o.scale_signal_g);
    addi(s, "hx_connected", o.hx711_connected);
    addi(s, "hx_ready", o.hx711_ready);
    addi(s, "hx_code", o.hx711_code);
    addi(s, "hx_seq", o.hx711_seq);
    add(s, "hx_sps", o.hx711_sps, 1);
    add(s, "fw_weight_g", st.fw_weight_g);
    add(s, "fw_flow_gps", st.fw_flow_gps);
    add(s, "fw_target_g", st.fw_target_g, 2);
    add(s, "fw_stop_offset_g", st.fw_stop_offset_g);
    add(s, "fw_latency_ms", st.fw_latency_ms, 1);
    addi(s, "fw_result", st.fw_result);
    addi(s, "fw_scale_fault", st.fw_scale_fault);
    addi(s, "fw_safety_stop", st.fw_safety_stop);
    add(s, "brightness", sim_display_brightness(), 2);
    addi(s, "display_on", sim_display_on());
    addi(s, "grinds", st.grinds_completed);
    adds(s, "operator", g_operator_running ? sim::operator_status() : std::string("manual"));
    addi(s, "restart", sim::world_restart_requested() ? 1 : 0);
    s.back() = '}';
    g_out = s;
    return g_out.c_str();
}

// Terminal grind records as JSON (run history).
TWIN_EXPORT const char* twin_records_json() {
    static const char* results[] = {"UNKNOWN", "SUCCESS", "OVERSHOOT", "MAX_PULSES", "TIMEOUT", "ERROR", "SCALE_ERROR"};
    std::string s = "[";
    for (const auto& r : sim::runtime_records()) {
        std::string o = "{";
        addi(o, "index", r.index);
        add(o, "t_start_s", r.t_start_s, 3);
        add(o, "t_end_s", r.t_end_s, 3);
        adds(o, "terminal", r.terminal_phase >= 0 ? sim::firmware_phase_name(r.terminal_phase) : "");
        adds(o, "result", r.terminal_phase >= 0 && r.result >= 0 && r.result < 7 ? results[r.result] : "");
        adds(o, "error", r.error);
        add(o, "target_g", r.target_g, 2);
        add(o, "fw_final_g", r.fw_final_seen ? r.fw_final_g : NAN, 2);
        add(o, "true_cup_end_g", r.true_cup_end_g, 3);
        add(o, "true_cup_g", r.open ? NAN : r.true_cup_closed_g, 3);
        addi(o, "pulses", r.pulses);
        add(o, "motor_on_s", r.motor_on_s, 3);
        add(o, "motor_invalid_signal_s", r.motor_invalid_signal_s, 3);
        add(o, "motor_after_end_s", r.motor_after_end_s, 3);
        addi(o, "open", r.open ? 1 : 0);
        o.back() = '}';
        s += o;
        s += ",";
    }
    if (s.size() > 1) s.pop_back();
    s += "]";
    g_out = s;
    return g_out.c_str();
}

TWIN_EXPORT const char* twin_log_read() {
    static char buf[65536];
    sim_log_read(buf, sizeof(buf));
    return buf;
}

TWIN_EXPORT const char* twin_trace_read() {
    static std::string chunk;
    chunk.resize(1 << 20);
    const size_t n = sim_trace_read(&chunk[0], chunk.size());
    chunk.resize(n);
    return chunk.c_str();
}

TWIN_EXPORT const char* twin_trace_header() { return sim::trace_header().c_str(); }

// Reset support for the page: export persistent state, re-instantiate the module, import.
TWIN_EXPORT double twin_persist_size() { return static_cast<double>(sim_persist_export(nullptr, 0)); }
TWIN_EXPORT int twin_persist_export(uint8_t* buf, double cap) {
    return static_cast<int>(sim_persist_export(buf, static_cast<size_t>(cap)));
}
TWIN_EXPORT int twin_persist_import(const uint8_t* buf, double len) {
    return sim_persist_import(buf, static_cast<size_t>(len));
}
TWIN_EXPORT void twin_trace_suppress_header() { sim::trace_suppress_header(); }
TWIN_EXPORT void twin_begin_post_reset(double observe_s) {
    sim::operator_start_post_reset(observe_s);
    g_operator_running = true;
}

// esp_reset_reason_t code of the pending restart (1 power-on, 3 software, 6 task watchdog).
TWIN_EXPORT int twin_restart_reason() { return sim::world_restart_reason(); }
TWIN_EXPORT int twin_operator_active() { return g_operator_running && !sim::operator_finished() ? 1 : 0; }

// Layout regression hooks (sim/qa/layout_audit.mjs).
TWIN_EXPORT int twin_ui(const char* cmd, const char* arg) { return sim::ui_command(cmd, arg ? arg : ""); }
TWIN_EXPORT const char* twin_layout_audit() {
    g_out = sim::layout_audit_json();
    return g_out.c_str();
}
