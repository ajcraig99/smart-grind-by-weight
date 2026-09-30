#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ESP_PARTITION_TYPE_APP = 0x00, ESP_PARTITION_TYPE_DATA = 0x01, ESP_PARTITION_TYPE_ANY = 0xff } esp_partition_type_t;
typedef enum {
    ESP_PARTITION_SUBTYPE_APP_FACTORY = 0x00, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10, ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
    ESP_PARTITION_SUBTYPE_DATA_OTA = 0x00, ESP_PARTITION_SUBTYPE_DATA_NVS = 0x02, ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82,
    ESP_PARTITION_SUBTYPE_DATA_LITTLEFS = 0x83, ESP_PARTITION_SUBTYPE_ANY = 0xff
} esp_partition_subtype_t;
typedef struct {
    void* flash_chip;
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;
const esp_partition_t* esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char* label);
esp_err_t esp_partition_read(const esp_partition_t* p, size_t offset, void* dst, size_t size);
esp_err_t esp_partition_write(const esp_partition_t* p, size_t offset, const void* src, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t* p, size_t offset, size_t size);
#ifdef __cplusplus
}
#endif
