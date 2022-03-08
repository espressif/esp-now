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

/**
 * @brief Enumerated list of provision authentication mode
 */
typedef enum {
    ESPNOW_PROV_AUTH_INVALID, /**< Invalid mode */
    ESPNOW_PROV_AUTH_PRODUCT, /**< Product authentication */
    ESPNOW_PROV_AUTH_DEVICE,  /**< Device authentication */
    ESPNOW_PROV_AUTH_CERT,    /**< Certificate authentication */
} espnow_prov_auth_mode_t;

/**
 * @brief Initiator information
 */
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

/**
 * @brief Responder information
 */
typedef struct {
    char product_id[16];
    char device_name[16];
} espnow_prov_responder_t;

/**
 * @brief WiFi configuration
 */
typedef struct {
    wifi_mode_t mode;          /**< WiFi mode */
    union {
        wifi_ap_config_t  ap;  /**< Configuration of AP */
        wifi_sta_config_t sta; /**< Configuration of STA */
    };
    char token[32];            /**< Token of the WiFi configuration */
    uint8_t custom_size;       /**< Customer data size */
    uint8_t custom_data[0];    /**< Customer data */
} espnow_prov_wifi_t;

/**
 * @brief  Initiator scans provision beacon
 *
 * @param[out]  responder_addr  mac address of responder
 * @param[out]  responder_info  information of responder
 * @param[out]  rx_ctrl  received packet radio metadata header
 * @param[in]  wait_ticks  the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_initator_scan(espnow_addr_t responder_addr, espnow_prov_responder_t *responder_info, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

/**
 * @brief  Initiator sends information to request WiFi configuration
 *
 * @param[in]  responder_addr  mac address of responder
 * @param[in]  initator_info  information of initiator
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_WIFI_TIMEOUT
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_initator_send(const espnow_addr_t responder_addr, const espnow_prov_initator_t *initator_info);

/**
 * @brief  Initiator receives WiFi configuration
 *
 * @param[out]  responder_addr  mac address of responder
 * @param[out]  wifi_config  WiFi configuration information from responder
 * @param[out]  rx_ctrl  received packet radio metadata header
 * @param[in]  wait_ticks  the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_initator_recv(espnow_addr_t responder_addr, espnow_prov_wifi_t *wifi_config, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

/**
 * @brief  Responder sends provision beacon
 *
 * @param[in]  responder_info  information of responder
 * @param[in]  wait_ticks  provision beacon sending time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_responder_beacon(const espnow_prov_responder_t *responder_info, TickType_t wait_ticks);

/**
 * @brief  Responder receives initiator information
 *
 * @param[out]  initator_addr  mac address of initiator
 * @param[out]  initator_info  information of initiator
 * @param[out]  rx_ctrl  received packet radio metadata header
 * @param[in]  wait_ticks  the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_responder_recv(espnow_addr_t initator_addr, espnow_prov_initator_t *initator_info, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

/**
 * @brief  Responder sends WiFi configuration
 *
 * @param[in]  initator_addr_list  mac address list of initiators
 * @param[in]  initator_addr_num  initiator address number
 * @param[in]  wifi_config  WiFi configuration information
 * 
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_responder_send(const espnow_addr_t *initator_addr_list, size_t initator_addr_num, const espnow_prov_wifi_t *wifi_config);

#ifdef __cplusplus
}
#endif
