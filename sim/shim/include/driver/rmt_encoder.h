#pragma once
#include "driver/rmt_types.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int reserved;
} rmt_copy_encoder_config_t;
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t* config, rmt_encoder_handle_t* ret_encoder);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
esp_err_t rmt_encoder_reset(rmt_encoder_handle_t encoder);
#ifdef __cplusplus
}
#endif
