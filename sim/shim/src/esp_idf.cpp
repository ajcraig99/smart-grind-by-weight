// ESP-IDF system services for the twin: errors, reset, heap, log, task watchdog, OTA
// partitions (a fixed two-slot table), app description, power locks, esp_timer.
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

#include "../../core/scheduler.h"
#include "../../core/world.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
esp_reset_reason_t g_reset_reason = ESP_RST_POWERON;
}

namespace sim {
void set_reset_reason(int reason) { g_reset_reason = static_cast<esp_reset_reason_t>(reason); }
}

extern "C" {

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED: return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
        case ESP_ERR_NVS_NOT_FOUND: return "ESP_ERR_NVS_NOT_FOUND";
        default: return "ESP_ERR_UNKNOWN";
    }
}

esp_reset_reason_t esp_reset_reason(void) { return g_reset_reason; }

void esp_restart(void) {
    sim::world_request_restart("esp_restart");
    // The chip stops executing this image. Park the caller; the runtime reboots the world.
    for (;;) {
        if (sim::in_task()) sim::block(sim::kNever);
        else return;
    }
}

uint32_t esp_get_free_heap_size(void) { return 180 * 1024; }
uint32_t esp_get_minimum_free_heap_size(void) { return 150 * 1024; }
uint32_t esp_random(void) { return sim::firmware_random(); }
void esp_fill_random(void* buf, size_t len) {
    uint8_t* out = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(sim::firmware_random());
}

// ---- heap ----
void* heap_caps_malloc(size_t size, uint32_t) { return std::malloc(size); }
void* heap_caps_calloc(size_t n, size_t size, uint32_t) { return std::calloc(n, size); }
void* heap_caps_realloc(void* ptr, size_t size, uint32_t) { return std::realloc(ptr, size); }
void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    const size_t rounded = (size + alignment - 1) / alignment * alignment;
    return std::aligned_alloc(alignment, rounded ? rounded : alignment);
}
void heap_caps_free(void* ptr) { std::free(ptr); }
size_t heap_caps_get_free_size(uint32_t caps) {
    return (caps & MALLOC_CAP_SPIRAM) ? 7u * 1024u * 1024u : 180u * 1024u;
}
size_t heap_caps_get_total_size(uint32_t caps) {
    return (caps & MALLOC_CAP_SPIRAM) ? 8u * 1024u * 1024u : 320u * 1024u;
}
size_t heap_caps_get_largest_free_block(uint32_t caps) {
    return (caps & MALLOC_CAP_SPIRAM) ? 4u * 1024u * 1024u : 110u * 1024u;
}
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps) - 30u * 1024u; }

// ---- log ----
void esp_log_level_set(const char*, esp_log_level_t) {}
void sim_esp_log(esp_log_level_t level, const char* tag, const char* format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    char line[320];
    const char* letter = level == ESP_LOG_ERROR ? "E" : level == ESP_LOG_WARN ? "W" : "I";
    const int n = std::snprintf(line, sizeof(line), "%s (%lu) %s: %s\n", letter,
                                static_cast<unsigned long>(sim::now_us() / 1000ULL), tag ? tag : "", message);
    if (n > 0) sim::log_write(line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
}

// ---- task watchdog: recorded only; the twin never panics on it ----
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t*) { return ESP_OK; }
esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t*) { return ESP_OK; }
esp_err_t esp_task_wdt_add(TaskHandle_t) { return ESP_OK; }
esp_err_t esp_task_wdt_delete(TaskHandle_t) { return ESP_OK; }
esp_err_t esp_task_wdt_reset(void) { return ESP_OK; }

// ---- partitions / OTA: running from ota_0, confirmed image ----
static esp_partition_t g_ota0 = {nullptr, ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, 0x10000, 0x600000, 4096, "app0", false, false};
static esp_partition_t g_ota1 = {nullptr, ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, 0x610000, 0x600000, 4096, "app1", false, false};
static esp_partition_t g_data = {nullptr, ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, 0xC10000, 0x100000, 4096, "patch", false, false};

const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char*) {
    if (type == ESP_PARTITION_TYPE_APP) return subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1 ? &g_ota1 : &g_ota0;
    return &g_data;
}
esp_err_t esp_partition_read(const esp_partition_t*, size_t, void* dst, size_t size) { std::memset(dst, 0xFF, size); return ESP_OK; }
esp_err_t esp_partition_write(const esp_partition_t*, size_t, const void*, size_t) { return ESP_OK; }
esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t, size_t) { return ESP_OK; }
const esp_partition_t* esp_ota_get_running_partition(void) { return &g_ota0; }
const esp_partition_t* esp_ota_get_boot_partition(void) { return &g_ota0; }
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return &g_ota1; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* state) {
    if (!partition || !state) return ESP_ERR_INVALID_ARG;
    *state = partition == &g_ota0 ? ESP_OTA_IMG_VALID : ESP_OTA_IMG_UNDEFINED;
    return ESP_OK;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { return ESP_OK; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { esp_restart(); return ESP_OK; }
static esp_app_desc_t g_app_desc = {0xABCD5432, 0, {0, 0}, "sim", "smart-grind-by-weight", "00:00:00", "Jan  1 2026", "sim", {0}, {0}};
esp_err_t esp_ota_get_partition_description(const esp_partition_t* partition, esp_app_desc_t* desc) {
    if (!partition || !desc) return ESP_ERR_INVALID_ARG;
    if (partition != &g_ota0) return ESP_ERR_NOT_FOUND;
    *desc = g_app_desc;
    return ESP_OK;
}
const esp_app_desc_t* esp_app_get_description(void) { return &g_app_desc; }
int esp_app_get_elf_sha256(char* dst, size_t size) {
    if (!dst || size == 0) return 0;
    std::snprintf(dst, size, "%s", "0000000000000000");
    return static_cast<int>(std::strlen(dst));
}
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t* out) { if (out) *out = 1; return ESP_OK; }
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t) { return ESP_OK; }
esp_err_t esp_ota_end(esp_ota_handle_t) { return ESP_ERR_OTA_VALIDATE_FAILED; }
esp_err_t esp_ota_abort(esp_ota_handle_t) { return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { return ESP_OK; }

// ---- power management locks ----
esp_err_t esp_pm_lock_create(esp_pm_lock_type_t, int, const char*, esp_pm_lock_handle_t* out) {
    if (out) *out = reinterpret_cast<esp_pm_lock_handle_t>(&g_app_desc);
    return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t) { return ESP_OK; }
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t) { return ESP_OK; }
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t) { return ESP_OK; }

}  // extern "C"

// ---- esp_timer on scheduler events ----

struct esp_timer {
    esp_timer_cb_t callback = nullptr;
    void* arg = nullptr;
    uint64_t period_us = 0;
    uint64_t event_id = 0;
    bool active = false;
    bool deleted = false;
};

static void esp_timer_fire(void* context) {
    esp_timer* timer = static_cast<esp_timer*>(context);
    timer->event_id = 0;
    if (!timer->active) return;
    if (timer->period_us > 0) {
        timer->event_id = sim::event_schedule(sim::now_us() + timer->period_us, esp_timer_fire, timer);
    } else {
        timer->active = false;
    }
    if (timer->callback) timer->callback(timer->arg);
}

extern "C" {

esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) {
    if (!args || !out) return ESP_ERR_INVALID_ARG;
    esp_timer* timer = new esp_timer();
    timer->callback = args->callback;
    timer->arg = args->arg;
    *out = timer;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (!timer) return ESP_ERR_INVALID_ARG;
    if (timer->active) return ESP_ERR_INVALID_STATE;
    timer->period_us = 0;
    timer->active = true;
    timer->event_id = sim::event_schedule(sim::now_us() + timeout_us, esp_timer_fire, timer);
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us) {
    if (!timer || period_us == 0) return ESP_ERR_INVALID_ARG;
    if (timer->active) return ESP_ERR_INVALID_STATE;
    timer->period_us = period_us;
    timer->active = true;
    timer->event_id = sim::event_schedule(sim::now_us() + period_us, esp_timer_fire, timer);
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (!timer) return ESP_ERR_INVALID_ARG;
    if (!timer->active) return ESP_ERR_INVALID_STATE;
    timer->active = false;
    if (timer->event_id) sim::event_cancel(timer->event_id);
    timer->event_id = 0;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (!timer) return ESP_ERR_INVALID_ARG;
    if (timer->active) esp_timer_stop(timer);
    delete timer;
    return ESP_OK;
}

int64_t esp_timer_get_time(void) {
    sim::note_clock_read();
    return static_cast<int64_t>(sim::now_us());
}

bool esp_timer_is_active(esp_timer_handle_t timer) { return timer && timer->active; }

}  // extern "C"
