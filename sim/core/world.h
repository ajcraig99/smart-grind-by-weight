// The physical world around the firmware: pins, HX711 serial protocol, relay, touch panel,
// framebuffer, and the plant model stepped once per virtual millisecond.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../sim_api.h"

namespace sim {

// ---- pins ----
void pin_mode(int pin, int arduino_mode);
void pin_write(int pin, int level);
int pin_read(int pin);
void pin_set_direction_output(int pin);   // gpio_set_direction(OUTPUT): routes the plain GPIO
void pin_set_pull(int pin, bool pullup, bool pulldown);
void pin_reset(int pin);
void pin_attach_rmt(int pin, bool attached);
void pin_set_rmt_level(int pin, int level);
int relay_pin_level();

// ---- plant ----
sim_plant_t* plant();
void plant_outputs(sim_plant_outputs_t* out);

// ---- touch ----
struct TouchState {
    int x = 0;
    int y = 0;
    bool pressed = false;
};
void touch_set(int x, int y, bool pressed);
TouchState touch_get();

// ---- display ----
constexpr int kScreenWidth = 280;
constexpr int kScreenHeight = 456;
uint16_t* framebuffer();
void framebuffer_mark_dirty();
bool framebuffer_take_dirty();
void display_set_brightness(uint8_t value);
float display_brightness();
void display_set_on(bool on);
bool display_is_on();

// ---- log ----
void log_write(const char* data, size_t len);
size_t log_read(char* buf, size_t cap);
void log_set_echo(bool echo_to_stderr);

// ---- deterministic randomness for firmware random()/esp_random() ----
uint32_t firmware_random();

// ---- lifecycle ----
void world_init(uint64_t seed);
bool world_restart_requested();
void world_request_restart(const char* reason);
uint64_t world_seed();

// Hook the runtime installs to observe each millisecond (trace sampling etc.).
using FrameObserver = void (*)(uint64_t boundary_us);
void world_set_frame_observer(FrameObserver observer);

}  // namespace sim
