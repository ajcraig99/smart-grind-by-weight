#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
void esp_log_level_set(const char* tag, esp_log_level_t level);
void sim_esp_log(esp_log_level_t level, const char* tag, const char* format, ...);
#define ESP_LOGE(tag, format, ...) sim_esp_log(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) sim_esp_log(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) sim_esp_log(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ((void)0)
#define ESP_LOGV(tag, format, ...) ((void)0)
#ifdef __cplusplus
}
#endif
