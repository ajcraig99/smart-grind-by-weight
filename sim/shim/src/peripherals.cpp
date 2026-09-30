// GPIO, RMT (relay waveform), I2C touch controller (FT3168) and the CO5300 panel.
#include <Arduino_GFX_Library.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/rmt_tx.h>

#include "../../core/scheduler.h"
#include "../../core/world.h"

#include <algorithm>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------- GPIO

extern "C" {

esp_err_t gpio_config(const gpio_config_t* config) {
    if (!config) return ESP_ERR_INVALID_ARG;
    for (int pin = 0; pin < 64; ++pin) {
        if (!(config->pin_bit_mask & (1ULL << pin))) continue;
        sim::pin_set_pull(pin, config->pull_up_en == GPIO_PULLUP_ENABLE, config->pull_down_en == GPIO_PULLDOWN_ENABLE);
        if (config->mode & GPIO_MODE_OUTPUT) sim::pin_set_direction_output(pin);
    }
    return ESP_OK;
}
esp_err_t gpio_reset_pin(gpio_num_t gpio) { sim::pin_reset(gpio); return ESP_OK; }
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level) { sim::pin_write(gpio, level ? 1 : 0); return ESP_OK; }
int gpio_get_level(gpio_num_t gpio) { return sim::pin_read(gpio); }
esp_err_t gpio_set_direction(gpio_num_t gpio, gpio_mode_t mode) {
    if (mode & GPIO_MODE_OUTPUT) sim::pin_set_direction_output(gpio);
    else sim::pin_mode(gpio, 0x01);
    return ESP_OK;
}
esp_err_t gpio_pullup_en(gpio_num_t gpio) { sim::pin_set_pull(gpio, true, false); return ESP_OK; }
esp_err_t gpio_pullup_dis(gpio_num_t gpio) { sim::pin_set_pull(gpio, false, false); return ESP_OK; }
esp_err_t gpio_pulldown_en(gpio_num_t gpio) { sim::pin_set_pull(gpio, false, true); return ESP_OK; }
esp_err_t gpio_pulldown_dis(gpio_num_t gpio) { sim::pin_set_pull(gpio, false, false); return ESP_OK; }

}  // extern "C"

// ---------------------------------------------------------------- RMT

struct rmt_encoder_t {
    int unused = 0;
};

struct rmt_channel_t {
    int gpio = -1;
    uint32_t resolution_hz = 1000000;
    bool enabled = false;
    bool active = false;
    bool infinite = false;
    std::vector<std::pair<int, uint64_t>> segments;  // (level, duration_us)
    size_t next_segment = 0;
    uint64_t event_id = 0;
    int eot_level = 0;
    rmt_tx_done_callback_t on_done = nullptr;
    void* user_ctx = nullptr;
};

namespace {

void rmt_step(void* context);

void rmt_stop(rmt_channel_t* ch) {
    if (ch->event_id) sim::event_cancel(ch->event_id);
    ch->event_id = 0;
    ch->active = false;
    ch->infinite = false;
    ch->segments.clear();
    sim::pin_set_rmt_level(ch->gpio, 0);
}

void rmt_step(void* context) {
    rmt_channel_t* ch = static_cast<rmt_channel_t*>(context);
    ch->event_id = 0;
    if (!ch->active) return;
    if (ch->next_segment >= ch->segments.size()) {
        sim::pin_set_rmt_level(ch->gpio, ch->eot_level);
        ch->active = false;
        ch->segments.clear();
        if (ch->on_done) {
            rmt_tx_done_event_data_t data{};
            ch->on_done(ch, &data, ch->user_ctx);
        }
        return;
    }
    const auto& seg = ch->segments[ch->next_segment++];
    sim::pin_set_rmt_level(ch->gpio, seg.first);
    ch->event_id = sim::event_schedule(sim::now_us() + seg.second, rmt_step, ch);
}

}  // namespace

extern "C" {

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* ret_chan) {
    if (!config || !ret_chan) return ESP_ERR_INVALID_ARG;
    rmt_channel_t* ch = new rmt_channel_t();
    ch->gpio = config->gpio_num;
    ch->resolution_hz = config->resolution_hz ? config->resolution_hz : 1000000;
    sim::pin_attach_rmt(ch->gpio, true);
    sim::pin_set_rmt_level(ch->gpio, 0);
    *ret_chan = ch;
    return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t ch) {
    if (!ch) return ESP_ERR_INVALID_ARG;
    rmt_stop(ch);
    sim::pin_attach_rmt(ch->gpio, false);
    sim::pin_mode(ch->gpio, 0x01);  // output driver disabled
    delete ch;
    return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t ch) {
    if (!ch) return ESP_ERR_INVALID_ARG;
    if (ch->enabled) return ESP_ERR_INVALID_STATE;
    ch->enabled = true;
    return ESP_OK;
}

esp_err_t rmt_disable(rmt_channel_handle_t ch) {
    if (!ch) return ESP_ERR_INVALID_ARG;
    if (!ch->enabled) return ESP_ERR_INVALID_STATE;
    rmt_stop(ch);  // aborts any transmission; the line returns to its idle level (LOW)
    ch->enabled = false;
    return ESP_OK;
}

esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t*, rmt_encoder_handle_t* ret_encoder) {
    if (!ret_encoder) return ESP_ERR_INVALID_ARG;
    *ret_encoder = new rmt_encoder_t();
    return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) { delete encoder; return ESP_OK; }
esp_err_t rmt_encoder_reset(rmt_encoder_handle_t encoder) { return encoder ? ESP_OK : ESP_ERR_INVALID_ARG; }

esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t ch, const rmt_tx_event_callbacks_t* cbs, void* user) {
    if (!ch || !cbs) return ESP_ERR_INVALID_ARG;
    ch->on_done = cbs->on_trans_done;
    ch->user_ctx = user;
    return ESP_OK;
}

esp_err_t rmt_transmit(rmt_channel_handle_t ch, rmt_encoder_handle_t encoder, const void* payload, size_t bytes,
                       const rmt_transmit_config_t* config) {
    if (!ch || !encoder || !payload || !config) return ESP_ERR_INVALID_ARG;
    if (!ch->enabled) return ESP_ERR_INVALID_STATE;
    if (ch->active) return ESP_ERR_INVALID_STATE;  // queue depth is not modelled; the firmware stops first
    const rmt_symbol_word_t* symbols = static_cast<const rmt_symbol_word_t*>(payload);
    const size_t count = bytes / sizeof(rmt_symbol_word_t);
    std::vector<std::pair<int, uint64_t>> segments;
    const double us_per_tick = 1e6 / static_cast<double>(ch->resolution_hz);
    bool terminated = false;
    for (size_t i = 0; i < count && !terminated; ++i) {
        const unsigned d0 = symbols[i].duration0, d1 = symbols[i].duration1;
        if (d0 == 0) { terminated = true; break; }
        segments.emplace_back(symbols[i].level0, static_cast<uint64_t>(d0 * us_per_tick + 0.5));
        if (d1 == 0) { terminated = true; break; }
        segments.emplace_back(symbols[i].level1, static_cast<uint64_t>(d1 * us_per_tick + 0.5));
    }
    if (segments.empty()) return ESP_ERR_INVALID_ARG;
    ch->active = true;
    ch->eot_level = config->flags.eot_level;
    if (config->loop_count < 0) {
        // Infinite loop: the relay stays at the pattern's level until rmt_disable().
        ch->infinite = true;
        sim::pin_set_rmt_level(ch->gpio, segments.front().first);
        return ESP_OK;
    }
    const int repeats = config->loop_count > 0 ? config->loop_count + 1 : 1;
    ch->segments.clear();
    for (int r = 0; r < repeats; ++r) ch->segments.insert(ch->segments.end(), segments.begin(), segments.end());
    ch->next_segment = 0;
    rmt_step(ch);
    return ESP_OK;
}

esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t ch, int timeout_ms) {
    if (!ch) return ESP_ERR_INVALID_ARG;
    if (!ch->active) return ESP_OK;
    if (timeout_ms == 0) return ESP_ERR_TIMEOUT;
    const uint64_t until = timeout_ms < 0 ? sim::kNever : sim::now_us() + static_cast<uint64_t>(timeout_ms) * 1000ULL;
    sim::block(until, [ch]() { return !ch->active; });
    return ch->active ? ESP_ERR_TIMEOUT : ESP_OK;
}

}  // extern "C"

// ---------------------------------------------------------------- I2C touch (FT3168 @ 0x38)

struct i2c_master_bus_t {
    int port = 0;
};
struct i2c_master_dev_t {
    uint16_t address = 0;
};

extern "C" {

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t* config, i2c_master_bus_handle_t* ret_bus) {
    if (!config || !ret_bus) return ESP_ERR_INVALID_ARG;
    *ret_bus = new i2c_master_bus_t{config->i2c_port};
    return ESP_OK;
}
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus) { delete bus; return ESP_OK; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t* config,
                                    i2c_master_dev_handle_t* ret_handle) {
    if (!bus || !config || !ret_handle) return ESP_ERR_INVALID_ARG;
    *ret_handle = new i2c_master_dev_t{config->device_address};
    return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle) { delete handle; return ESP_OK; }

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t* write_buffer, size_t write_size,
                                      uint8_t* read_buffer, size_t read_size, int) {
    if (!dev || !write_buffer || write_size < 1 || !read_buffer) return ESP_ERR_INVALID_ARG;
    std::memset(read_buffer, 0, read_size);
    if (dev->address != 0x38) return ESP_ERR_TIMEOUT;
    // An I2C read of ~6 bytes at 300 kHz takes ~0.2 ms of bus time.
    sim::busy_advance_us(200);
    if (write_buffer[0] == 0x02 && read_size >= 5) {
        const sim::TouchState t = sim::touch_get();
        if (t.pressed) {
            const int x = std::max(0, std::min(t.x, sim::kScreenWidth - 1));
            const int y = std::max(0, std::min(t.y, sim::kScreenHeight - 1));
            read_buffer[0] = 1;
            read_buffer[1] = static_cast<uint8_t>(0x80 | ((x >> 8) & 0x0F));  // event flag: contact
            read_buffer[2] = static_cast<uint8_t>(x & 0xFF);
            read_buffer[3] = static_cast<uint8_t>((y >> 8) & 0x0F);
            read_buffer[4] = static_cast<uint8_t>(y & 0xFF);
        }
    }
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t*, size_t, int) {
    return dev ? ESP_OK : ESP_ERR_INVALID_ARG;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t* read_buffer, size_t read_size, int) {
    if (!dev || !read_buffer) return ESP_ERR_INVALID_ARG;
    std::memset(read_buffer, 0, read_size);
    return ESP_OK;
}

}  // extern "C"

// ---------------------------------------------------------------- CO5300 panel via Arduino_GFX

bool Arduino_GFX::begin(int32_t) {
    sim::display_set_on(true);
    return true;
}

void Arduino_GFX::fillScreen(uint16_t color) {
    uint16_t* fb = sim::framebuffer();
    std::fill(fb, fb + sim::kScreenWidth * sim::kScreenHeight, color);
    sim::framebuffer_mark_dirty();
}

static void blit(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h, bool big_endian) {
    uint16_t* fb = sim::framebuffer();
    for (int16_t row = 0; row < h; ++row) {
        const int yy = y + row;
        if (yy < 0 || yy >= sim::kScreenHeight) continue;
        for (int16_t col = 0; col < w; ++col) {
            const int xx = x + col;
            if (xx < 0 || xx >= sim::kScreenWidth) continue;
            const uint16_t p = bitmap[row * w + col];
            fb[yy * sim::kScreenWidth + xx] = big_endian ? static_cast<uint16_t>((p >> 8) | (p << 8)) : p;
        }
    }
    sim::framebuffer_mark_dirty();
}

// Arduino_GFX 1.6.7: draw16bitRGBBitmap converts native pixels to the panel's big-endian order
// (Arduino_ESP32QSPI::writePixels, MSB_32_16_16_SET), draw16bitBeRGBBitmap sends bytes as they are
// (writeBytes). LVGL 9.5 byte-swaps the flushed buffer when LV_COLOR_16_SWAP is set
// (lv_refr.c:1433-1434), which include/lv_conf.h does for the device build. The virtual panel
// stores native RGB565, so the big-endian path is swapped back here.
void Arduino_GFX::draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) {
    blit(x, y, bitmap, w, h, false);
}

void Arduino_GFX::draw16bitBeRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) {
    blit(x, y, bitmap, w, h, true);
}

void Arduino_GFX::displayOn() { sim::display_set_on(true); }
void Arduino_GFX::displayOff() { sim::display_set_on(false); }
void Arduino_CO5300::setBrightness(uint8_t brightness) { sim::display_set_brightness(brightness); }
