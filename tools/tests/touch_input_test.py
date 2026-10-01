"""Run the production touch driver and grind-button tap guards with fakes."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


I2C = r'''
#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t = int;
using gpio_num_t = int;
constexpr int I2C_NUM_0 = 0, I2C_CLK_SRC_DEFAULT = 0, I2C_ADDR_BIT_LEN_7 = 0;
struct i2c_master_bus_config_t {
    int i2c_port; gpio_num_t sda_io_num, scl_io_num; int clk_source, glitch_ignore_cnt, intr_priority;
    size_t trans_queue_depth; struct { unsigned enable_internal_pullup:1, allow_pd:1; } flags;
};
struct i2c_device_config_t {
    int dev_addr_length; uint16_t device_address; uint32_t scl_speed_hz, scl_wait_us;
    struct { unsigned disable_ack_check:1; } flags;
};
using i2c_master_bus_handle_t = void*;
using i2c_master_dev_handle_t = void*;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t*);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t*, i2c_master_dev_handle_t*);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, size_t, uint8_t*, size_t, int);
'''
ESP_ERR = r'''
#pragma once
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 0x103, ESP_ERR_TIMEOUT = 0x107, ESP_FAIL = -1;
inline const char* esp_err_to_name(int) { return "error"; }
'''
ESP_LOG = r'''
#pragma once
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
constexpr int ESP_LOG_NONE = 0;
inline void esp_log_level_set(const char*, int) {}
'''
ARDUINO = r'''
#pragma once
#include <cstdint>
inline uint32_t fake_millis = 1000;
inline unsigned long millis() { return fake_millis; }
'''
CONSTANTS = r'''
#pragma once
#define HW_TOUCH_I2C_SDA_PIN 47
#define HW_TOUCH_I2C_SCL_PIN 48
#define HW_TOUCH_I2C_ADDRESS 0x38
#define HW_DISPLAY_WIDTH_PX 280
#define HW_DISPLAY_HEIGHT_PX 456
#define DEBUG_SUPPRESS_TOUCH_I2C_ERRORS 1
'''
TOUCH_CASES = r'''
#include "hardware/touch_driver.h"
#include "esp_err.h"
#include <cassert>
#include <cstring>
#include <deque>
struct Read { esp_err_t err; uint8_t bytes[5]; };
std::deque<Read> reads;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t*, i2c_master_bus_handle_t* bus) { *bus = (void*)1; return ESP_OK; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t*, i2c_master_dev_handle_t* dev) {
    *dev = (void*)2; return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t*, size_t, uint8_t* out, size_t size, int) {
    assert(!reads.empty() && size == 5);
    const Read read = reads.front(); reads.pop_front();
    if (read.err == ESP_OK) std::memcpy(out, read.bytes, size);
    return read.err;
}
const Read idle{ESP_OK, {0, 0, 0, 0, 0}};
const Read garbage{ESP_OK, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};          // NACKed read, ACK check off
const Read off_screen{ESP_OK, {1, 0x0F, 0xFF, 0x0F, 0xFF}};          // x = y = 4095
const Read failed{ESP_ERR_TIMEOUT, {}};
const Read bad_count{ESP_OK, {9, 0, 50, 0, 50}};                    // no controller reports 9 points
const Read touch_at(uint16_t x, uint16_t y) {
    return {ESP_OK, {1, uint8_t(x >> 8), uint8_t(x & 0xFF), uint8_t(y >> 8), uint8_t(y & 0xFF)}};
}
bool step(TouchDriver& driver, const Read& read) { reads.push_back(read); driver.update(); return driver.is_pressed(); }
int main() {
    TouchDriver driver; driver.init();
    // Idle, garbage and off-screen reads are never presses.
    assert(!step(driver, idle));
    assert(!step(driver, garbage));
    assert(!step(driver, off_screen));
    assert(!step(driver, failed));
    assert(!step(driver, bad_count));
    // A press survives one or two unusable reads without a release.
    assert(step(driver, touch_at(100, 200)));
    assert(driver.get_touch_data().just_pressed);
    driver.consume_press_event();
    assert(step(driver, failed));
    assert(step(driver, garbage));
    assert(step(driver, touch_at(101, 201)));
    TouchData held = driver.get_touch_data();
    assert(held.x == 101 && held.y == 201 && !held.just_pressed);  // still the same press
    // Reads that keep failing release it on the third.
    assert(step(driver, failed));
    assert(step(driver, off_screen));
    assert(!step(driver, failed));
    // A normal release is immediate.
    assert(step(driver, touch_at(10, 10)));
    assert(!step(driver, idle));
    assert(reads.empty());
}
'''
TAP_CASES = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#define USER_BUTTON_REARM_MS 700
#define USER_BUTTON_DRAG_CANCEL_PX 30
#define LV_ABS(x) ((x) > 0 ? (x) : (-(x)))
struct lv_point_t { int32_t x, y; };
struct lv_indev_t { lv_point_t point; };
struct lv_event_t { lv_indev_t* indev; };
lv_indev_t* lv_event_get_indev(lv_event_t* e) { return e->indev; }
void lv_indev_get_point(lv_indev_t* indev, lv_point_t* point) { *point = indev->point; }
int icon_sets = 0;
void lv_img_set_src(void*, const void*) { ++icon_sets; }
uint32_t now = 10000;
uint32_t millis() { return now; }
enum class UIState { READY, GRINDING, PURGE_CONFIRM, GRIND_COMPLETE, REFILL_CONFIRM };
struct State { UIState current = UIState::READY; bool is_state(UIState s) const { return current == s; } };
struct UIManager { State* state_machine; };
#define LV_SYMBOL_SETTINGS "SETTINGS"
#define LV_SYMBOL_WIFI "WIFI"
#define LV_SYMBOL_PAUSE "PAUSE"
struct GrindingUIController {
    UIManager* ui_manager_;
    void* grind_icon_ = nullptr;
    void* pulse_icon_ = nullptr;
    lv_point_t press_point_{0, 0};
    uint32_t press_ms_ = 0;
    const char* grind_symbol_ = nullptr;
    const char* pulse_symbol_ = nullptr;
    uint32_t grind_button_changed_ms_ = 0;
    uint32_t pulse_button_changed_ms_ = 0;
    void record_press(lv_event_t* e);
    bool is_deliberate_tap(lv_event_t* e, uint32_t changed_ms, bool rearm) const;
    bool grind_button_stops() const;
    bool grind_button_needs_rearm() const;
    bool pulse_button_stops() const;
    void set_grind_icon(const char* symbol);
};
''' + "{methods}" + r'''
int main() {
    State state; UIManager ui{&state}; GrindingUIController c{&ui};
    lv_indev_t touch{{140, 400}}; lv_event_t event{&touch};
    // The button turns into PLAY (after STOP or OK): a press starting within
    // 700 ms is the second tap of a double tap and is ignored; later ones count.
    c.set_grind_icon("PLAY");
    const uint32_t changed = c.grind_button_changed_ms_;
    assert(icon_sets == 1 && changed == now && c.grind_button_needs_rearm());
    now += 699; c.record_press(&event); now += 50;
    assert(!c.is_deliberate_tap(&event, changed, true));
    now = changed + 700; c.record_press(&event); now += 50;
    assert(c.is_deliberate_tap(&event, changed, true));
    // A press that began before the change was aimed at the old meaning, even
    // when it is released long after (holding OK while the screen moves on).
    c.record_press(&event); now += 10; c.set_grind_icon("OK"); now += 5000;
    assert(!c.is_deliberate_tap(&event, c.grind_button_changed_ms_, true));
    assert(!c.is_deliberate_tap(&event, c.grind_button_changed_ms_, false));
    // Re-setting the same icon is not a change of meaning.
    c.set_grind_icon("OK"); assert(icon_sets == 2);
    // Opening the menu or Wi-Fi page needs no re-arm delay.
    c.set_grind_icon(LV_SYMBOL_SETTINGS); assert(!c.grind_button_needs_rearm());
    c.record_press(&event); assert(c.is_deliberate_tap(&event, c.grind_button_changed_ms_, false));
    c.set_grind_icon(LV_SYMBOL_WIFI); assert(!c.grind_button_needs_rearm());
    // A press that travels more than 30 px is a swipe.
    now += 1000; c.record_press(&event);
    touch.point = {140 + 31, 400}; assert(!c.is_deliberate_tap(&event, 0, true));
    touch.point = {140, 400 - 30}; assert(c.is_deliberate_tap(&event, 0, true));
    // PAUSE stops the motor, so it is exempt like STOP; RESUME is not.
    c.pulse_symbol_ = LV_SYMBOL_PAUSE; assert(c.pulse_button_stops());
    c.pulse_symbol_ = "PLAY"; assert(!c.pulse_button_stops());
    // STOP never waits: the button stops while grinding or at the purge or refill prompt.
    for (UIState s : {UIState::GRINDING, UIState::PURGE_CONFIRM, UIState::REFILL_CONFIRM}) {
        state.current = s; assert(c.grind_button_stops());
    }
    for (UIState s : {UIState::READY, UIState::GRIND_COMPLETE}) { state.current = s; assert(!c.grind_button_stops()); }
}
'''


def build_and_run(sources, code, include_dirs):
    with tempfile.TemporaryDirectory(prefix="smart-grind-touch-test-") as folder:
        root = Path(folder)
        cases = root / "cases.cpp"
        cases.write_text(code)
        binary = root / "test"
        flags = []
        for directory in include_dirs(root):
            flags += ["-I", str(directory)]
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                        *flags, str(cases), *[str(s(root)) for s in sources], "-o", str(binary)],
                       check=True)
        subprocess.run([str(binary)], check=True, timeout=20)


class TouchInputTest(unittest.TestCase):
    def test_touch_driver_holds_presses_through_bad_reads(self):
        def layout(root):
            (root / "src/hardware").mkdir(parents=True)
            (root / "src/config").mkdir(parents=True)
            (root / "include/driver").mkdir(parents=True)
            for name in ("touch_driver.h", "touch_driver.cpp"):
                shutil.copy(ROOT / "src/hardware" / name, root / "src/hardware" / name)
            (root / "src/config/constants.h").write_text(CONSTANTS)
            (root / "include/driver/i2c_master.h").write_text(I2C)
            (root / "include/esp_err.h").write_text(ESP_ERR)
            (root / "include/esp_log.h").write_text(ESP_LOG)
            (root / "include/Arduino.h").write_text(ARDUINO)
            return [root / "include", root / "src"]
        build_and_run([lambda root: root / "src/hardware/touch_driver.cpp"], TOUCH_CASES, layout)

    def test_grind_button_tap_guards(self):
        source = (ROOT / "src/ui/controllers/grinding_controller.cpp").read_text()
        methods = "\n".join(function(source, signature) for signature in (
            "void GrindingUIController::record_press(",
            "bool GrindingUIController::is_deliberate_tap(",
            "bool GrindingUIController::grind_button_stops()",
            "bool GrindingUIController::grind_button_needs_rearm()",
            "bool GrindingUIController::pulse_button_stops()",
            "void GrindingUIController::set_grind_icon(",
        ))
        code = "#include <initializer_list>\n" + TAP_CASES.replace("{methods}", methods)
        build_and_run([], code, lambda root: [])


if __name__ == "__main__":
    unittest.main()
