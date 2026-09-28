#include "firmware_validation.h"

#include <Arduino.h>
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
bool validation_done = false;
}

namespace FirmwareValidation {

void update(uint32_t now_ms, bool tasks_healthy) {
    if (validation_done || now_ms < SYS_FIRMWARE_VALIDATION_DELAY_MS || !tasks_healthy) return;
    validation_done = true;

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;  // USB-flashed or already confirmed image.
    }
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    LOG_BLE("[BOOT] Updated firmware %s after %lus of healthy running (%s)\n",
            result == ESP_OK ? "confirmed" : "could not be confirmed",
            static_cast<unsigned long>(now_ms / 1000U), esp_err_to_name(result));
}

}  // namespace FirmwareValidation
