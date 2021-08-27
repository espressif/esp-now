
#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include <esp_err.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C"
{
#endif


esp_err_t web_server_send(const uint8_t *addr, const char *data,
                          size_t size, const wifi_pkt_rx_ctrl_t *rx_ctrl);
esp_err_t web_server_start(const char *base_path);

#ifdef __cplusplus
}
#endif
