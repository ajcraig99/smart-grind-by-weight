// CSV trace sampled from the frame observer. Fixed-point formatting keeps output byte-stable.
#include "runtime.h"
#include "world.h"
#include "scheduler.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace sim {

namespace {
uint32_t g_period_ms = 10;
std::string g_buffer;
bool g_header_pending = true;
const std::string g_header =
    "t_ms,phase,ui_state,relay_pin,relay_contact,motor_speed,flow_burr_gps,flow_cup_gps,"
    "m_hopper_g,m_burr_g,m_chute_g,m_inflight_g,m_cup_g,m_platform_g,m_spilled_g,cup_present,"
    "scale_true_g,scale_signal_g,hx_code,hx_seq,hx_connected,fw_weight_g,fw_flow_gps,fw_target_g,"
    "fw_stop_offset_g,fw_latency_ms,fw_has_sample,touch_pressed\n";
constexpr size_t kTraceCap = 64u << 20;
}  // namespace

const std::string& trace_header() { return g_header; }
void trace_suppress_header() { g_header_pending = false; }

void trace_set_period_ms(uint32_t period) {
    g_period_ms = period;
    g_header_pending = true;
    g_buffer.clear();
}

void trace_on_frame(uint64_t boundary_us, const FirmwareView& v) {
    if (g_period_ms == 0) return;
    const uint64_t ms = boundary_us / 1000ULL;
    if (ms % g_period_ms != 0) return;
    if (g_header_pending) {
        g_buffer += g_header;
        g_header_pending = false;
    }
    sim_plant_outputs_t o;
    plant_outputs(&o);
    char row[512];
    const int n = std::snprintf(
        row, sizeof(row),
        "%llu,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f,%.4f,%ld,%lu,%d,%.4f,%.4f,%.2f,%.4f,%.1f,%d,%d\n",
        static_cast<unsigned long long>(ms), v.phase, v.ui_state, relay_pin_level(), o.relay_contact, o.motor_speed,
        o.flow_burr_gps, o.flow_cup_gps, o.m_hopper_g, o.m_burr_g, o.m_chute_g, o.m_inflight_g, o.m_cup_g,
        o.m_platform_g, o.m_spilled_g, o.cup_present, o.scale_true_g, o.scale_signal_g,
        static_cast<long>(o.hx711_code), static_cast<unsigned long>(o.hx711_seq), o.hx711_connected, v.weight_g,
        v.flow_gps, v.target_g, v.stop_offset_g, v.latency_ms, v.has_recent_sample, touch_get().pressed ? 1 : 0);
    if (n <= 0) return;
    if (g_buffer.size() + static_cast<size_t>(n) > kTraceCap) return;  // reader too slow: drop
    g_buffer.append(row, static_cast<size_t>(n));
}

size_t trace_read(char* buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    const size_t n = g_buffer.size() < cap - 1 ? g_buffer.size() : cap - 1;
    std::memcpy(buf, g_buffer.data(), n);
    buf[n] = '\0';
    g_buffer.erase(0, n);
    return n;
}

}  // namespace sim
