#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t esp_ota_handle_t;
typedef enum {
    ESP_OTA_IMG_NEW = 0x0U, ESP_OTA_IMG_PENDING_VERIFY = 0x1U, ESP_OTA_IMG_VALID = 0x2U,
    ESP_OTA_IMG_INVALID = 0x3U, ESP_OTA_IMG_ABORTED = 0x4U, ESP_OTA_IMG_UNDEFINED = 0xFFFFFFFFU
} esp_ota_img_states_t;
#define OTA_SIZE_UNKNOWN 0xffffffff
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
const esp_partition_t* esp_ota_get_running_partition(void);
const esp_partition_t* esp_ota_get_boot_partition(void);
const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t* start_from);
esp_err_t esp_ota_get_state_partition(const esp_partition_t* partition, esp_ota_img_states_t* state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
esp_err_t esp_ota_get_partition_description(const esp_partition_t* partition, esp_app_desc_t* desc);
esp_err_t esp_ota_begin(const esp_partition_t* partition, size_t image_size, esp_ota_handle_t* out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition);
#ifdef __cplusplus
}
#endif
