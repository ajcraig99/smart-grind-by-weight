// Scripted user. Drives the firmware only through its real inputs: taps on the virtual
// touchscreen at the production button positions, and physical actions on the plant
// (beans, cup). Reads UI state/phase only to decide when to act.
#include "json.h"
#include "runtime.h"
#include "scheduler.h"
#include "world.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace sim {
namespace {

// UIState values (src/system/state_machine.h).
constexpr int kUiReady = 0;
constexpr int kUiGrinding = 1;
constexpr int kUiComplete = 2;
constexpr int kUiTimeout = 3;
constexpr int kUiPurgeConfirm = 8;
// GrindPhase values (src/controllers/grind_events.h).
constexpr int kPhaseIdle = 0;
constexpr int kPhaseCompleted = 13;
constexpr int kPhaseTimeout = 14;

// Grind-screen buttons: 100x100 circles aligned LV_ALIGN_BOTTOM_MID with y offset -10 on a
// 280x456 panel (src/ui/controllers/grinding_controller.cpp build_controls/update_button_layout):
// centre y = 456 - 10 - 50 = 396; single layout x = 140; dual layout x = 140 -/+ 60.
constexpr int kButtonY = 396;
constexpr int kButtonCentreX = 140;
constexpr int kButtonLeftX = 80;
constexpr int kButtonRightX = 200;

struct Event {
    std::string when = "start";  // t | boot | start | continue | phase:NAME
    double delay_s = 0;
    std::string kind;            // fault | action | reset | touch
    int code = 0;
    int active = 1;
    double value = 0;
    bool fired = false;
};

struct Scenario {
    std::string name = "normal";
    double target_g = 18.0;
    double beans_g = 18.0;
    double cup_mass_g = 0;       // <= 0: plant default
    int purge_mode = 1;          // grinder_mode preference: 0 Prime, 1 Purge (default)
    std::string purge_action = "discard";
    bool cup_at_boot = false;
    double reaction_s = 1.2;
    double boot_timeout_s = 30;
    double grind_timeout_s = 150;
    int grinds = 1;
    double settle_after_s = 3.0;
    double tap_hold_s = 0.1;
    std::vector<Event> events;
};

Scenario g_sc;
bool g_finished = false;
std::string g_status = "idle";
double g_anchor_boot = -1, g_anchor_start = -1, g_anchor_continue = -1;
std::vector<std::pair<std::string, double>> g_phase_anchors;  // phase name -> first entry after start
int g_last_phase_seen = -1;

double now_s() { return static_cast<double>(now_us()) / 1e6; }

void wait_s(double s) {
    if (s <= 0) return;
    block(now_us() + static_cast<uint64_t>(s * 1e6 + 0.5));
}

template <typename F>
bool wait_until(F cond, double timeout_s) {
    const uint64_t until = now_us() + static_cast<uint64_t>(timeout_s * 1e6);
    while (!cond()) {
        if (now_us() >= until) return false;
        block(now_us() + 1000);  // re-check every millisecond
    }
    return true;
}

void tap(int x, int y) {
    touch_set(x, y, true);
    wait_s(g_sc.tap_hold_s);
    touch_set(x, y, false);
}

int ui() { return runtime_last_view().ui_state; }
int phase() { return runtime_last_view().phase; }
bool terminal() { return phase() == kPhaseCompleted || phase() == kPhaseTimeout; }

int fault_code(const std::string& name) {
    if (name == "lc_disconnect") return SIM_FAULT_LC_DISCONNECT;
    if (name == "lc_stuck") return SIM_FAULT_LC_STUCK;
    if (name == "lc_noise_burst") return SIM_FAULT_LC_NOISE_BURST;
    if (name == "relay_stuck_on") return SIM_FAULT_RELAY_STUCK_ON;
    if (name == "relay_stuck_off") return SIM_FAULT_RELAY_STUCK_OFF;
    if (name == "motor_stall") return SIM_FAULT_MOTOR_STALL;
    if (name == "feed_block") return SIM_FAULT_FEED_BLOCK;
    return 0;
}

int action_code(const std::string& name) {
    if (name == "place_cup") return SIM_ACT_PLACE_CUP;
    if (name == "remove_cup") return SIM_ACT_REMOVE_CUP;
    if (name == "empty_cup") return SIM_ACT_EMPTY_CUP;
    if (name == "load_beans") return SIM_ACT_LOAD_BEANS;
    if (name == "bump") return SIM_ACT_BUMP;
    if (name == "press") return SIM_ACT_PRESS;
    if (name == "clean_chute") return SIM_ACT_CLEAN_CHUTE;
    if (name == "wipe_platform") return SIM_ACT_WIPE_PLATFORM;
    return 0;
}

double anchor_time(const std::string& when) {
    if (when == "t") return 0;
    if (when == "boot") return g_anchor_boot;
    if (when == "start") return g_anchor_start;
    if (when == "continue") return g_anchor_continue;
    if (when.compare(0, 6, "phase:") == 0) {
        for (const auto& pa : g_phase_anchors) if (pa.first == when.substr(6)) return pa.second;
    }
    return -1;
}

void fire(Event& e) {
    e.fired = true;
    char line[160];
    std::snprintf(line, sizeof(line), "[SIM %.3fs] event %s code=%d active=%d value=%.3f (when %s+%.3fs)\n",
                  now_s(), e.kind.c_str(), e.code, e.active, e.value, e.when.c_str(), e.delay_s);
    log_write(line, std::strlen(line));
    if (e.kind == "fault") {
        runtime_note_fault(e.code, e.active);
        plant_fault(plant(), e.code, e.active, e.value);
    } else if (e.kind == "action") {
        plant_action(plant(), e.code, e.value);
    } else if (e.kind == "reset") {
        // value: esp_reset_reason_t the next boot reports; default ESP_RST_POWERON (power cut).
        world_set_restart_reason(e.value > 0 ? static_cast<int>(e.value) : 1);
        world_request_restart("scenario reset");
    }
}

void operator_task(void*) {
    g_status = "waiting for boot";
    if (g_sc.cup_at_boot) plant_action(plant(), SIM_ACT_PLACE_CUP, g_sc.cup_mass_g);
    // Ready for a user: main screen shown and the scale delivering samples (boot tare follows).
    if (!wait_until([] { return runtime_booted() && ui() == kUiReady && runtime_last_view().has_recent_sample; },
                    g_sc.boot_timeout_s)) {
        g_status = "boot timeout (ui state " + std::to_string(ui()) + ")";
        g_finished = true;
        block(kNever);
    }
    g_anchor_boot = now_s();
    wait_s(2.0);
    for (int i = 0; i < g_sc.grinds; ++i) {
        g_status = "loading";
        plant_action(plant(), SIM_ACT_LOAD_BEANS, g_sc.beans_g);
        sim_plant_outputs_t out;
        plant_outputs(&out);
        if (!out.cup_present) {
            plant_action(plant(), SIM_ACT_PLACE_CUP, g_sc.cup_mass_g);
            wait_s(1.0);  // let the reading settle before pressing start
        }
        wait_s(g_sc.reaction_s);
        g_status = "start";
        tap(kButtonCentreX, kButtonY);
        g_anchor_start = now_s();
        g_phase_anchors.clear();
        if (!wait_until([] { return phase() != kPhaseIdle; }, 2.0)) {
            g_status = "start refused";
            break;
        }
        bool purge_handled = false;
        const bool done = wait_until(
            [&purge_handled] {
                if (!purge_handled && ui() == kUiPurgeConfirm) return true;
                return terminal();
            },
            g_sc.grind_timeout_s);
        if (done && !purge_handled && ui() == kUiPurgeConfirm) {
            purge_handled = true;
            g_status = "purge prompt";
            wait_s(g_sc.reaction_s);
            if (g_sc.purge_action == "discard") {
                plant_action(plant(), SIM_ACT_REMOVE_CUP, 0);
                wait_s(0.8);
                plant_action(plant(), SIM_ACT_EMPTY_CUP, 0);
                wait_s(0.8);
                plant_action(plant(), SIM_ACT_PLACE_CUP, 0);
                wait_s(g_sc.reaction_s);
            }
            if (ui() == kUiPurgeConfirm) {
                tap(kButtonRightX, kButtonY);
                g_anchor_continue = now_s();
            }
            g_status = "grinding";
            wait_until([] { return terminal(); }, g_sc.grind_timeout_s);
        }
        if (!terminal()) {
            g_status = "grind did not finish";
            break;
        }
        g_status = "result";
        wait_s(g_sc.settle_after_s);
        // OK / dismiss: single centred button after a weight grind, left button otherwise.
        const bool dual = ui() == kUiComplete && runtime_last_view().mode == 1;
        tap(dual ? kButtonLeftX : kButtonCentreX, kButtonY);
        wait_until([] { return phase() == kPhaseIdle; }, 3.0);
        runtime_close_records();
        if (i + 1 < g_sc.grinds) {
            plant_action(plant(), SIM_ACT_REMOVE_CUP, 0);
            wait_s(0.8);
            plant_action(plant(), SIM_ACT_EMPTY_CUP, 0);
            wait_s(0.8);
            plant_action(plant(), SIM_ACT_PLACE_CUP, 0);
            wait_s(2.0);
        }
    }
    (void)kUiGrinding;
    (void)kUiTimeout;
    if (g_status == "result") g_status = "done";
    g_finished = true;
    block(kNever);
}

}  // namespace

void operator_on_frame() {
    const int ph = phase();
    if (ph != g_last_phase_seen) {
        g_last_phase_seen = ph;
        if (g_anchor_start >= 0) {
            const char* name = firmware_phase_name(ph);
            bool seen = false;
            for (const auto& pa : g_phase_anchors) if (pa.first == name) seen = true;
            if (!seen) g_phase_anchors.emplace_back(name, now_s());
        }
    }
    for (Event& e : g_sc.events) {
        if (e.fired) continue;
        const double anchor = anchor_time(e.when);
        if (anchor < 0) continue;
        if (now_s() + 1e-9 >= anchor + e.delay_s) fire(e);
    }
}

bool operator_load_scenario_json(const std::string& text, std::string* error) {
    Json root;
    if (!json_parse(text, root, error) || !root.is_obj()) {
        if (error && error->empty()) *error = "scenario must be a JSON object";
        return false;
    }
    Scenario sc;
    sc.name = root.str("name", sc.name);
    sc.target_g = root.num("target_g", sc.target_g);
    sc.beans_g = root.num("beans_g", sc.target_g);
    sc.cup_mass_g = root.num("cup_mass_g", sc.cup_mass_g);
    sc.purge_mode = static_cast<int>(root.num("purge_mode", sc.purge_mode));
    sc.purge_action = root.str("purge_action", sc.purge_action);
    sc.cup_at_boot = root.num("cup_at_boot", 0) != 0;
    sc.reaction_s = root.num("reaction_s", sc.reaction_s);
    sc.boot_timeout_s = root.num("boot_timeout_s", sc.boot_timeout_s);
    sc.grind_timeout_s = root.num("grind_timeout_s", sc.grind_timeout_s);
    sc.grinds = static_cast<int>(root.num("grinds", sc.grinds));
    sc.settle_after_s = root.num("settle_after_s", sc.settle_after_s);
    if (const Json* events = root.get("events")) {
        for (const Json& ej : events->a) {
            Event e;
            e.when = ej.str("when", "start");
            e.delay_s = ej.num("delay_s", 0);
            e.active = static_cast<int>(ej.num("active", 1));
            e.value = ej.num("value", 0);
            if (ej.get("fault")) {
                e.kind = "fault";
                e.code = fault_code(ej.str("fault", ""));
            } else if (ej.get("action")) {
                const std::string a = ej.str("action", "");
                if (a == "reset") {
                    e.kind = "reset";
                } else {
                    e.kind = "action";
                    e.code = action_code(a);
                }
            }
            if (e.kind.empty() || (e.kind != "reset" && e.code == 0)) {
                if (error) *error = "unknown event in scenario";
                return false;
            }
            sc.events.push_back(e);
        }
    }
    g_sc = sc;
    return true;
}

void operator_set_target(float target_g) { g_sc.target_g = target_g; }

// Pre-boot preferences so the grind uses the scenario's target and purge mode; the load cell is
// marked calibrated with the plant's nominal counts per gram (plant lc_gain_error models error).
void operator_seed_preferences() {
    int ok = 0;
    const double cpg = plant_get_param(plant(), "lc_counts_per_g", &ok);
    sim_seed_setting_float("grinder", "hx_cal", static_cast<float>(ok ? cpg : -7050.0));
    sim_seed_setting_bool("load_cell", "calibrated", 1);
    sim_seed_setting_int("grinder", "profile", 1);
    sim_seed_setting_float("grinder", "weight1", static_cast<float>(g_sc.target_g));
    sim_seed_setting_int("grinder", "grind_mode", 0);
    sim_seed_setting_int("grinder", "grinder_mode", g_sc.purge_mode);
}

void operator_start() {
    g_finished = false;
    g_status = "starting";
    g_anchor_boot = g_anchor_start = g_anchor_continue = -1;
    g_phase_anchors.clear();
    g_last_phase_seen = -1;
    for (Event& e : g_sc.events) e.fired = false;
    task_create("operator", operator_task, nullptr, 20, 1, 256 * 1024);
}

namespace {
double g_observe_s = 20.0;
void post_reset_task(void*) {
    g_status = "post-reset boot";
    runtime_begin_observation();
    const bool ready = wait_until([] { return runtime_booted() && ui() == kUiReady; }, g_sc.boot_timeout_s);
    g_status = ready ? "post-reset ready" : "post-reset not ready (ui " + std::to_string(ui()) + ")";
    wait_s(g_observe_s);
    runtime_close_records();
    g_finished = true;
    block(kNever);
}
}  // namespace

void operator_start_post_reset(double observe_s) {
    g_finished = false;
    g_observe_s = observe_s;
    task_create("operator", post_reset_task, nullptr, 20, 1, 256 * 1024);
}

bool operator_finished() { return g_finished; }
std::string operator_status() { return g_status; }

}  // namespace sim
