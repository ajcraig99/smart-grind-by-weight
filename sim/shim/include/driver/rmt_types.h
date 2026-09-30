#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rmt_channel_t* rmt_channel_handle_t;
typedef struct rmt_encoder_t rmt_encoder_t;
typedef rmt_encoder_t* rmt_encoder_handle_t;
typedef enum { RMT_CLK_SRC_DEFAULT = 0, RMT_CLK_SRC_APB = 1, RMT_CLK_SRC_XTAL = 2 } rmt_clock_source_t;
typedef union {
    struct {
        uint16_t duration0 : 15;
        uint16_t level0 : 1;
        uint16_t duration1 : 15;
        uint16_t level1 : 1;
    };
    uint32_t val;
} rmt_symbol_word_t;
typedef struct {
    size_t num_symbols;
} rmt_tx_done_event_data_t;
typedef bool (*rmt_tx_done_callback_t)(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t* edata, void* user_ctx);
#ifdef __cplusplus
}
#endif
