#include "runtime.h"

#include "json.h"
#include "scheduler.h"
#include "world.h"

#include "../shim/src/littlefs_store.h"
#include "../shim/src/nvs_store.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace sim {
void set_reset_reason(int reason);
}

namespace sim {
namespace {

constexpr int kPhaseIdle = 0;
constexpr int kPhaseInitializing = 1;
constexpr int kPhaseTaring = 3;
constexpr int kPhasePulseExecute = 7;
constexpr int kPhaseCompleted = 13;
constexpr int kPhaseTimeout = 14;
constexpr int kPhasePurgeConfirm = 17;

bool g_created = false;
bool g_booted = false;
bool g_setup_done = false;
std::vector<GrindRecord> g_records;
FirmwareView g_view;
int g_last_phase = -1;
bool g_fault_active[16] = {};
double g_current_run_s = 0;
std::string g_log_line;
bool g_log_hooked = false;

void loop_task(void*) {
    firmware_setup();
    g_setup_done = true;
    for (;;) firmware_loop();
}

void on_phase_change(int from, int to, double t_s, const sim_plant_outputs_t& out) {
    GrindRecord* rec = runtime_open_record();
    if (from == kPhaseIdle && to != kPhaseIdle) {
        GrindRecord r;
        r.index = static_cast<uint32_t>(g_records.size());
        r.t_start_s = t_s;
        r.target_g = g_view.target_g;
        r.loaded_g = out.m_loaded_g;
        g_records.push_back(r);
        rec = &g_records.back();
    }
    if (!rec) return;
    if (to == kPhasePulseExecute) ++rec->pulses;
    if (to == kPhasePurgeConfirm) ++rec->purge_prompts;
    if (to == kPhaseTaring) ++rec->tares;
    if ((to == kPhaseCompleted || to == kPhaseTimeout) && rec->terminal_phase < 0) {
        rec->terminal_phase = to;
        rec->t_end_s = t_s;
        rec->result = g_view.result;
        rec->true_cup_end_g = out.m_cup_g;
        rec->latency_ms = g_view.latency_ms;
        rec->stop_offset_g = g_view.stop_offset_g;
        rec->target_g = g_view.target_g;
    }
    if (to == kPhaseIdle) {
        rec->t_closed_s = t_s;
        rec->true_cup_closed_g = out.m_cup_g;
        rec->true_platform_closed_g = out.m_platform_g;
        rec->burr_left_g = out.m_burr_g;
        rec->chute_g = out.m_chute_g;
        rec->spilled_g = out.m_spilled_g;
        rec->open = false;
    }
}

void account_motor(const sim_plant_outputs_t& out, double dt) {
    GrindRecord* rec = runtime_open_record();
    if (out.relay_contact) g_current_run_s += dt;
    else g_current_run_s = 0;
    if (!rec || !out.relay_contact) return;
    rec->motor_on_s += dt;
    if (rec->t_motor_first_s < 0) rec->t_motor_first_s = static_cast<double>(now_us()) / 1e6;
    if (g_current_run_s > rec->motor_on_max_run_s) rec->motor_on_max_run_s = g_current_run_s;
    if (g_fault_active[SIM_FAULT_LC_DISCONNECT] || g_fault_active[SIM_FAULT_LC_STUCK] || !out.hx711_connected) {
        rec->motor_invalid_signal_s += dt;
    }
    if (!g_view.has_recent_sample) rec->motor_no_sample_s += dt;
    if (rec->terminal_phase >= 0) rec->motor_after_end_s += dt;
    if (!out.cup_present) rec->motor_no_cup_s += dt;
}

void on_frame(uint64_t boundary_us) {
    if (!g_created) return;
    g_view = firmware_view();
    sim_plant_outputs_t out;
    plant_outputs(&out);
    const double t_s = static_cast<double>(boundary_us) / 1e6;
    if (g_last_phase < 0) g_last_phase = g_view.phase;
    if (g_view.phase != g_last_phase) {
        on_phase_change(g_last_phase, g_view.phase, t_s, out);
        g_last_phase = g_view.phase;
    }
    account_motor(out, 0.001);
    operator_on_frame();
    trace_on_frame(boundary_us, g_view);
}

// Firmware log lines carry what the UI showed: "GRIND COMPLETE - Final settled weight captured: %.2fg"
// and "GRIND ERROR - %s, Weight: %.2fg" (src/ui/controllers/grinding_controller.cpp).
void on_log_line(const std::string& line) {
    GrindRecord* rec = runtime_open_record();
    if (!rec) return;
    const char* complete = "GRIND COMPLETE - Final settled weight captured: ";
    const char* error = "GRIND ERROR - ";
    size_t pos;
    if ((pos = line.find(complete)) != std::string::npos) {
        rec->fw_final_g = std::strtof(line.c_str() + pos + std::strlen(complete), nullptr);
        rec->fw_final_seen = true;
    } else if ((pos = line.find(error)) != std::string::npos) {
        const std::string rest = line.substr(pos + std::strlen(error));
        const size_t comma = rest.rfind(", Weight: ");
        rec->error = comma == std::string::npos ? rest : rest.substr(0, comma);
        if (comma != std::string::npos) {
            rec->fw_final_g = std::strtof(rest.c_str() + comma + 10, nullptr);
            rec->fw_final_seen = true;
        }
    }
}

}  // namespace

void runtime_log_observer(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '\n') {
            on_log_line(g_log_line);
            g_log_line.clear();
        } else if (g_log_line.size() < 1024) {
            g_log_line.push_back(data[i]);
        }
    }
}

void runtime_create(uint64_t seed) {
    reset(0);
    world_init(seed);
    nvs_store().clear();
    fs_store().files.clear();
    fs_store().dirs.clear();
    fs_store().mounted = false;
    g_records.clear();
    g_view = FirmwareView{};
    g_last_phase = -1;
    std::memset(g_fault_active, 0, sizeof(g_fault_active));
    g_current_run_s = 0;
    g_log_line.clear();
    g_booted = false;
    g_setup_done = false;
    world_set_frame_observer(on_frame);
    g_created = true;
}

void runtime_boot() {
    if (!g_created || g_booted) return;
    // Arduino-ESP32 app_main: loopTask, priority 1, core 1
    // (framework-arduinoespressif32 3.3.2 cores/esp32/main.cpp:113).
    task_create("loopTask", loop_task, nullptr, 1, 1, 0);
    g_booted = true;
}

void runtime_run_ms(uint32_t ms) {
    if (!g_created) return;
    run_until(now_us() + static_cast<uint64_t>(ms) * 1000ULL);
}

bool runtime_booted() { return g_setup_done; }
uint64_t runtime_now_us() { return now_us(); }
const std::vector<GrindRecord>& runtime_records() { return g_records; }

GrindRecord* runtime_open_record() {
    if (g_records.empty() || !g_records.back().open) return nullptr;
    return &g_records.back();
}

FirmwareView runtime_last_view() { return g_view; }

void runtime_close_records() {
    GrindRecord* rec = runtime_open_record();
    if (!rec) return;
    sim_plant_outputs_t out;
    plant_outputs(&out);
    rec->t_closed_s = static_cast<double>(now_us()) / 1e6;
    rec->true_cup_closed_g = out.m_cup_g;
    rec->true_platform_closed_g = out.m_platform_g;
    rec->burr_left_g = out.m_burr_g;
    rec->chute_g = out.m_chute_g;
    rec->spilled_g = out.m_spilled_g;
    rec->open = false;
}

void runtime_note_fault(int fault, int active) {
    if (fault > 0 && fault < 16) g_fault_active[fault] = active != 0;
}
bool runtime_fault_active(int fault) { return fault > 0 && fault < 16 && g_fault_active[fault]; }

}  // namespace sim

// ================================================================== C API (sim_api.h)

extern "C" {

int sim_create(uint64_t seed) {
    sim::runtime_create(seed);
    return 0;
}

int sim_set_param(const char* name, double v) {
    if (!name) return -1;
    if (std::strcmp(name, "trace_period_ms") == 0) {
        sim::trace_set_period_ms(static_cast<uint32_t>(v));
        return 0;
    }
    return plant_set_param(sim::plant(), name, v);
}

int sim_load_params_json(const char* json) {
    if (!json) return -1;
    sim::Json root;
    std::string err;
    if (!sim::json_parse(json, root, &err) || !root.is_obj()) return -1;
    int failures = 0;
    for (const auto& kv : root.o) {
        double value;
        if (kv.second.is_num()) value = kv.second.n;
        else if (kv.second.is_obj() && kv.second.get("value") && kv.second.get("value")->is_num()) value = kv.second.get("value")->n;
        else continue;
        if (sim_set_param(kv.first.c_str(), value) != 0) ++failures;
    }
    return failures == 0 ? 0 : -failures;
}

void sim_seed_setting_float(const char* ns, const char* key, float v) {
    Preferences p;
    if (p.begin(ns, false)) { p.putFloat(key, v); p.end(); }
}
void sim_seed_setting_int(const char* ns, const char* key, int32_t v) {
    Preferences p;
    if (p.begin(ns, false)) { p.putInt(key, v); p.end(); }
}
void sim_seed_setting_bool(const char* ns, const char* key, int v) {
    Preferences p;
    if (p.begin(ns, false)) { p.putBool(key, v != 0); p.end(); }
}

void sim_boot(void) { sim::runtime_boot(); }
void sim_run_ms(uint32_t ms) { sim::runtime_run_ms(ms); }

void sim_get_state(sim_state_t* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->t_us = sim::now_us();
    sim::plant_outputs(&out->plant);
    out->relay_pin_level = sim::relay_pin_level();
    const sim::FirmwareView v = sim::runtime_last_view();
    out->fw_phase = v.phase;
    out->ui_state = v.ui_state;
    out->fw_mode = v.mode;
    out->fw_weight_g = v.weight_g;
    out->fw_display_weight_g = v.weight_g;
    out->fw_flow_gps = v.flow_gps;
    out->fw_target_g = v.target_g;
    out->fw_stop_offset_g = v.stop_offset_g;
    out->fw_latency_ms = v.latency_ms;
    out->fw_result = v.result;
    out->fw_scale_fault = v.scale_fault;
    out->fw_safety_stop = v.safety_stop;
    out->booted = sim::runtime_booted() ? 1 : 0;
    out->fb_dirty = 0;
    uint32_t done = 0;
    for (const auto& r : sim::runtime_records()) if (r.terminal_phase >= 0) ++done;
    out->grinds_completed = done;
}

const char* sim_phase_name(int phase) { return sim::firmware_phase_name(phase); }
const char* sim_ui_state_name(int ui_state) { return sim::firmware_ui_state_name(ui_state); }

void sim_touch(int x, int y, int pressed) { sim::touch_set(x, y, pressed != 0); }
const uint16_t* sim_framebuffer(void) { return sim::framebuffer(); }
int sim_framebuffer_take_dirty(void) { return sim::framebuffer_take_dirty() ? 1 : 0; }
float sim_display_brightness(void) { return sim::display_brightness(); }
int sim_display_on(void) { return sim::display_is_on() ? 1 : 0; }

void sim_action(int action, double value) { plant_action(sim::plant(), action, value); }
void sim_fault(int fault, int active, double value) {
    sim::runtime_note_fault(fault, active);
    plant_fault(sim::plant(), fault, active, value);
}

size_t sim_log_read(char* buf, size_t cap) { return sim::log_read(buf, cap); }
void sim_trace_period_ms(uint32_t period_ms) { sim::trace_set_period_ms(period_ms); }
size_t sim_trace_read(char* buf, size_t cap) { return sim::trace_read(buf, cap); }

// Persistent state layout: "SGT1", u64 t_us, u32 nvs_len, nvs, u32 fs_len, fs, u32 plant_len, plant.
size_t sim_persist_export(uint8_t* buf, size_t cap) {
    std::vector<uint8_t> out = {'S', 'G', 'T', '1'};
    auto put_u32 = [&out](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    const uint64_t t = sim::now_us();
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(t >> (8 * i)));
    const auto nvs = sim::nvs_store().serialize();
    put_u32(static_cast<uint32_t>(nvs.size()));
    out.insert(out.end(), nvs.begin(), nvs.end());
    const auto fs = sim::fs_store().serialize();
    put_u32(static_cast<uint32_t>(fs.size()));
    out.insert(out.end(), fs.begin(), fs.end());
    const size_t plant_len = plant_sizeof();
    put_u32(static_cast<uint32_t>(plant_len));
    const uint8_t* plant_bytes = reinterpret_cast<const uint8_t*>(sim::plant());
    out.insert(out.end(), plant_bytes, plant_bytes + plant_len);
    if (buf && cap >= out.size()) std::memcpy(buf, out.data(), out.size());
    return out.size();
}

int sim_persist_import(const uint8_t* buf, size_t len) {
    if (!buf || len < 12 || std::memcmp(buf, "SGT1", 4) != 0) return -1;
    size_t pos = 4;
    uint64_t t = 0;
    for (int i = 0; i < 8; ++i) t |= static_cast<uint64_t>(buf[pos++]) << (8 * i);
    auto get_u32 = [&](uint32_t& v) {
        if (pos + 4 > len) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(buf[pos++]) << (8 * i);
        return true;
    };
    uint32_t n;
    if (!get_u32(n) || pos + n > len || !sim::nvs_store().deserialize(buf + pos, n)) return -2;
    pos += n;
    if (!get_u32(n) || pos + n > len || !sim::fs_store().deserialize(buf + pos, n)) return -3;
    pos += n;
    if (!get_u32(n) || n != plant_sizeof() || pos + n > len) return -4;
    std::memcpy(reinterpret_cast<uint8_t*>(sim::plant()), buf + pos, n);
    sim::reset(t);
    sim::set_reset_reason(3 /* ESP_RST_SW */);
    return 0;
}

}  // extern "C"
