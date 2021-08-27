// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_PROV_CUSTOM_MAX_SIZE  64

typedef enum {
    ESPNOW_PROV_AUTH_INVALID, /**< Invalid mode */
    ESPNOW_PROV_AUTH_PRODUCT, /**< Key authentication */
    ESPNOW_PROV_AUTH_DEVICE,  /**< Key authentication */
    ESPNOW_PROV_AUTH_CERT,    /**< Certificate authentication */
} espnow_prov_auth_mode_t;

typedef struct {
    char product_id[16];
    char device_name[16];
    espnow_prov_auth_mode_t auth_mode;
    union {
        char device_secret[32];
        char product_secret[32];
        char cert_secret[32];
    };
    uint8_t custom_size;
    uint8_t custom_data[0];
} espnow_prov_initator_t;

typedef struct {
    char product_id[16];
    char device_name[16];
} espnow_prov_responder_t;

typedef struct {
    wifi_mode_t mode;
    union {
        wifi_ap_config_t  ap;  /**< configuration of AP */
        wifi_sta_config_t sta; /**< configuration of STA */
    };
    char token[32];
    uint8_t custom_size;
    uint8_t custom_data[0];
} espnow_prov_wifi_t;

esp_err_t espnow_prov_initator_scan(espnow_addr_t responder_addr, espnow_prov_responder_t *responder_info, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);
esp_err_t espnow_prov_initator_send(const espnow_addr_t responder_addr, const espnow_prov_initator_t *initator_info);
esp_err_t espnow_prov_initator_recv(espnow_addr_t responder_addr, espnow_prov_wifi_t *wifi_config, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

esp_err_t espnow_prov_responder_beacon(const espnow_prov_responder_t *responder_info, TickType_t wait_ticks);
esp_err_t espnow_prov_responder_recv(espnow_addr_t initator_addr, espnow_prov_initator_t *initator_info, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);
esp_err_t espnow_prov_responder_send(const espnow_addr_t *initator_addr_list, size_t initator_addr_num, const espnow_prov_wifi_t *wifi_config);

#ifdef __cplusplus
}
#endif
