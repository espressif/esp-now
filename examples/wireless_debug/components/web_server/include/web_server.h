/* HTTP Restful API Server - declarations

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

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
