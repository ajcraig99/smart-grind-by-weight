#include "firmware_validation.h"

#include <Arduino.h>
#include <atomic>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_ota_ops.h>

#include "../config/constants.h"

// The Arduino core defines this hook weakly (C linkage) and, while it returns
// false, marks a pending image valid during startup. Returning true leaves the
// decision to FirmwareValidation::update().
extern "C" bool verifyRollbackLater() {
    return true;
}

namespace {
constexpr uint32_t RETRY_INTERVAL_MS = 5000;
constexpr size_t IMAGE_ID_BYTES = 8;

std::atomic<bool> validation_done{false};
bool retry_scheduled = false;
uint32_t retry_at_ms = 0;

bool is_pending(const esp_partition_t* partition) {
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    return partition && esp_ota_get_state_partition(partition, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

String hex_id(const uint8_t* sha256) {
    char text[IMAGE_ID_BYTES * 2 + 1];
    for (size_t i = 0; i < IMAGE_ID_BYTES; ++i) {
        snprintf(text + 2 * i, 3, "%02x", sha256[i]);
    }
    return String(text);
}

void schedule_retry(uint32_t now_ms) {
    retry_scheduled = true;
    retry_at_ms = now_ms + RETRY_INTERVAL_MS;
}
}  // namespace

namespace FirmwareValidation {

bool update(uint32_t now_ms, bool tasks_healthy) {
    if (validation_done.load() || now_ms < SYS_FIRMWARE_VALIDATION_DELAY_MS || !tasks_healthy) {
        return false;
    }
    if (retry_scheduled && static_cast<int32_t>(now_ms - retry_at_ms) < 0) return false;

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!is_pending(running)) {
        validation_done.store(true);
        return true;  // USB-flashed or already confirmed image.
    }
    // A further update installed since this boot is now the boot selection,
    // and the confirmation would apply to it. Updates wait for confirmation,
    // so this only guards against one that did not.
    if (esp_ota_get_boot_partition() != running) {
        schedule_retry(now_ms);
        return false;
    }
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result != ESP_OK) {
        // Still pending, so the next reset would roll back a good image.
        LOG_BLE("[BOOT] Updated firmware could not be confirmed (%s); retrying\n",
                esp_err_to_name(result));
        schedule_retry(now_ms);
        return false;
    }
    LOG_BLE("[BOOT] Updated firmware confirmed after %lus of healthy running\n",
            static_cast<unsigned long>(now_ms / 1000U));
    validation_done.store(true);
    return true;
}

bool running_image_pending() {
    return !validation_done.load() && is_pending(esp_ota_get_running_partition());
}

String image_id(const esp_partition_t* partition) {
    esp_app_desc_t description{};
    if (!partition || esp_ota_get_partition_description(partition, &description) != ESP_OK) {
        return String();
    }
    return hex_id(description.app_elf_sha256);
}

String running_image_id() {
    const esp_app_desc_t* description = esp_app_get_description();
    return description ? hex_id(description->app_elf_sha256) : String();
}

}  // namespace FirmwareValidation
