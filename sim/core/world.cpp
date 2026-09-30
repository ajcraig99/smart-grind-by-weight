#include "world.h"

#include "scheduler.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// Pin assignments come from the firmware's own configuration (read-only include).
#include "config/hardware.h"

namespace sim {
namespace {

// Arduino-ESP32 pin mode bits (Arduino.h): INPUT 0x01, OUTPUT 0x03, PULLUP 0x04, PULLDOWN 0x08.
constexpr int kModeInput = 0x01;
constexpr int kModeOutputBit = 0x02;
constexpr int kModePullup = 0x04;
constexpr int kModePulldown = 0x08;

struct Pin {
    bool output = false;
    int out_level = 0;
    bool pullup = false;
    bool pulldown = false;
    bool rmt_attached = false;
    int rmt_level = 0;
};

std::map<int, Pin> g_pins;

// HX711 serial state (the chip side of the 2-wire protocol).
struct Hx711Wire {
    int sck_level = 0;
    uint64_t sck_high_since_us = 0;
    bool reading = false;
    int edges = 0;               // rising edges since the read started
    uint32_t latched = 0;        // 24-bit two's complement shifted MSB first
    int dout_bit = 1;
    bool powered = true;
} g_hx;

std::vector<uint8_t> g_plant_storage;
sim_plant_t* g_plant = nullptr;

TouchState g_touch;
uint16_t g_fb[kScreenWidth * kScreenHeight];
bool g_fb_dirty = true;
uint8_t g_brightness = 255;
bool g_display_on = true;

std::string g_log;
bool g_log_echo = false;
constexpr size_t kLogCap = 1u << 20;

uint64_t g_seed = 1;
uint64_t g_fw_rng = 0;
bool g_restart_requested = false;
uint64_t g_fw_epoch_us = 0;
int g_restart_reason = 3;  // ESP_RST_SW
FrameObserver g_frame_observer = nullptr;

constexpr uint64_t kHx711PowerDownUs = 60;  // HX711: SCK high > 60 us enters power down

bool hx711_powered_now() {
    if (g_hx.sck_level && sim::now_us() - g_hx.sck_high_since_us > kHx711PowerDownUs) return false;
    return true;
}

void update_hx_power() {
    const bool powered = hx711_powered_now();
    if (!powered && g_hx.powered) {
        // Power down resets the serial interface.
        g_hx.reading = false;
        g_hx.edges = 0;
    }
    g_hx.powered = powered;
}

void on_frame(uint64_t boundary_us) {
    update_hx_power();
    sim_plant_inputs_t in{};
    in.relay_cmd = relay_pin_level();
    in.hx711_powered = g_hx.powered ? 1 : 0;
    plant_step(g_plant, &in, SIM_PLANT_DT_S);
    if (g_frame_observer) g_frame_observer(boundary_us);
}

void hx711_sck_rising() {
    sim_plant_outputs_t out;
    plant_get_outputs(g_plant, &out);
    if (!out.hx711_connected || !g_hx.powered) return;
    if (!g_hx.reading) {
        if (!out.hx711_ready) return;  // clocking while DOUT is high does nothing useful
        g_hx.reading = true;
        g_hx.edges = 0;
        g_hx.latched = static_cast<uint32_t>(out.hx711_code) & 0xFFFFFFu;
        plant_hx711_consume(g_plant);
    }
    ++g_hx.edges;
    if (g_hx.edges <= 24) {
        g_hx.dout_bit = static_cast<int>((g_hx.latched >> (24 - g_hx.edges)) & 1u);
    } else {
        // The 25th pulse pulls DOUT high; 26th/27th only select the next gain.
        g_hx.dout_bit = 1;
        g_hx.reading = false;
    }
}

uint64_t splitmix(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

}  // namespace

// ---------------------------------------------------------------- pins

void pin_mode(int pin, int mode) {
    Pin& p = g_pins[pin];
    p.output = (mode & kModeOutputBit) != 0;
    p.pullup = (mode & kModePullup) != 0;
    p.pulldown = (mode & kModePulldown) != 0;
    (void)kModeInput;
    if (p.output) p.rmt_attached = false;
    if (pin == HW_LOADCELL_SCK_PIN) pin_write(pin, p.out_level);
}

void pin_write(int pin, int level) {
    Pin& p = g_pins[pin];
    level = level ? 1 : 0;
    const int previous = p.out_level;
    p.out_level = level;
    if (pin == HW_LOADCELL_SCK_PIN) {
        update_hx_power();
        const int sck = p.output ? level : 0;
        if (sck && !g_hx.sck_level) {
            g_hx.sck_high_since_us = sim::now_us();
            g_hx.sck_level = 1;
            hx711_sck_rising();
        } else if (!sck && g_hx.sck_level) {
            g_hx.sck_level = 0;
            update_hx_power();
        }
    }
    (void)previous;
}

int pin_read(int pin) {
    if (pin == HW_LOADCELL_DOUT_PIN) {
        update_hx_power();
        sim_plant_outputs_t out;
        plant_get_outputs(g_plant, &out);
        const Pin& p = g_pins[pin];
        if (!out.hx711_connected) return p.pullup ? 1 : 0;  // floating line on the ESP32 pull resistor
        if (!g_hx.powered) return 1;
        if (g_hx.reading) return g_hx.dout_bit;
        return out.hx711_ready ? 0 : 1;
    }
    const Pin& p = g_pins[pin];
    if (p.output) return p.rmt_attached ? p.rmt_level : p.out_level;
    return p.pullup ? 1 : 0;
}

void pin_set_direction_output(int pin) {
    Pin& p = g_pins[pin];
    p.output = true;
    p.rmt_attached = false;  // gpio_set_direction routes the plain GPIO signal (grinder.cpp comment)
}

void pin_set_pull(int pin, bool pullup, bool pulldown) {
    Pin& p = g_pins[pin];
    p.pullup = pullup;
    p.pulldown = pulldown;
}

void pin_reset(int pin) {
    Pin& p = g_pins[pin];
    p = Pin{};
    p.pullup = true;  // gpio_reset_pin enables the pull-up and disables the output
    if (pin == HW_LOADCELL_SCK_PIN) pin_write(pin, 0);
}

void pin_attach_rmt(int pin, bool attached) {
    Pin& p = g_pins[pin];
    p.rmt_attached = attached;
    if (attached) p.output = true;
}

void pin_set_rmt_level(int pin, int level) { g_pins[pin].rmt_level = level ? 1 : 0; }

int relay_pin_level() {
    auto it = g_pins.find(HW_MOTOR_RELAY_PIN);
    if (it == g_pins.end()) return 0;
    const Pin& p = it->second;
    if (!p.output) return p.pullup && !p.pulldown ? 1 : 0;  // an undriven pulled-up pin energises
    return p.rmt_attached ? p.rmt_level : p.out_level;
}

// ---------------------------------------------------------------- plant

sim_plant_t* plant() { return g_plant; }
void plant_outputs(sim_plant_outputs_t* out) { plant_get_outputs(g_plant, out); }

// ---------------------------------------------------------------- touch

void touch_set(int x, int y, bool pressed) {
    g_touch.x = x;
    g_touch.y = y;
    g_touch.pressed = pressed;
}
TouchState touch_get() { return g_touch; }

// ---------------------------------------------------------------- display

uint16_t* framebuffer() { return g_fb; }
void framebuffer_mark_dirty() { g_fb_dirty = true; }
bool framebuffer_take_dirty() {
    const bool dirty = g_fb_dirty;
    g_fb_dirty = false;
    return dirty;
}
void display_set_brightness(uint8_t value) { g_brightness = value; }
float display_brightness() { return static_cast<float>(g_brightness) / 255.0f; }
void display_set_on(bool on) {
    g_display_on = on;
    g_fb_dirty = true;
}
bool display_is_on() { return g_display_on; }

// ---------------------------------------------------------------- log

void runtime_log_observer(const char* data, size_t len);

void log_write(const char* data, size_t len) {
    if (g_log_echo) std::fwrite(data, 1, len, stderr);
    runtime_log_observer(data, len);
    if (g_log.size() + len > kLogCap) g_log.erase(0, g_log.size() + len - kLogCap);
    g_log.append(data, len);
}

size_t log_read(char* buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    const size_t n = g_log.size() < cap - 1 ? g_log.size() : cap - 1;
    std::memcpy(buf, g_log.data(), n);
    buf[n] = '\0';
    g_log.erase(0, n);
    return n;
}

void log_set_echo(bool echo) { g_log_echo = echo; }

uint32_t firmware_random() { return static_cast<uint32_t>(splitmix(g_fw_rng) >> 32); }

// ---------------------------------------------------------------- lifecycle

void world_init(uint64_t seed) {
    g_seed = seed;
    g_fw_rng = seed ^ 0xA5A5A5A5DEADBEEFULL;
    g_pins.clear();
    g_hx = Hx711Wire{};
    g_plant_storage.assign(plant_sizeof(), 0);
    g_plant = reinterpret_cast<sim_plant_t*>(g_plant_storage.data());
    plant_init(g_plant, seed);
    g_touch = TouchState{};
    std::memset(g_fb, 0, sizeof(g_fb));
    g_fb_dirty = true;
    g_brightness = 255;
    g_display_on = true;
    g_log.clear();
    g_restart_requested = false;
    g_fw_epoch_us = 0;
    g_restart_reason = 3;
    set_frame_hook(on_frame);
}

bool world_restart_requested() { return g_restart_requested; }
uint64_t firmware_now_us() { return sim::now_us() - g_fw_epoch_us; }
void set_firmware_epoch_us(uint64_t epoch_us) { g_fw_epoch_us = epoch_us; }
uint64_t firmware_epoch_us() { return g_fw_epoch_us; }

void world_request_restart(const char* reason) {
    g_restart_requested = true;
    request_stop();
    char line[160];
    const int n = std::snprintf(line, sizeof(line), "[SIM] restart requested: %s\n", reason ? reason : "");
    if (n > 0) log_write(line, static_cast<size_t>(n));
}
uint64_t world_seed() { return g_seed; }
void world_set_restart_reason(int code) { g_restart_reason = code; }
int world_restart_reason() { return g_restart_reason; }
void world_set_frame_observer(FrameObserver observer) { g_frame_observer = observer; }

}  // namespace sim
