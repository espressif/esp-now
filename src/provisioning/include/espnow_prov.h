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
#include "espnow.h"

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
typedef struct espnow_prov_initiator_s {
    char product_id[16];                /**< Product ID */
    char device_name[16];               /**< Device name */
    espnow_prov_auth_mode_t auth_mode;  /**< Authentication mode of provisioning */
    union {
        char device_secret[32];         /**< Device security key */
        char product_secret[32];        /**< Product security key */
        char cert_secret[32];           /**< Certify security key */
    };
    uint8_t custom_size;                /**< Customer data size */
    uint8_t custom_data[0];             /**< Customer data */
} ESPNOW_PACKED_STRUCT espnow_prov_initiator_t;

/**
 * @brief Responder information
 */
typedef struct espnow_prov_responder_s {
    char product_id[16];                /**< Product ID */
    char device_name[16];               /**< Device name */
} ESPNOW_PACKED_STRUCT espnow_prov_responder_t;

/**
 * @brief WiFi configuration
 */
typedef struct espnow_prov_wifi_s {
    wifi_mode_t mode;          /**< WiFi mode */
    union {
        wifi_ap_config_t  ap;  /**< Configuration of AP */
        wifi_sta_config_t sta; /**< Configuration of STA */
    };
    char token[32];            /**< Token of the WiFi configuration */
    uint8_t custom_size;       /**< Customer data size */
    uint8_t custom_data[0];    /**< Customer data */
} ESPNOW_PACKED_STRUCT espnow_prov_wifi_t;

/**
 * @brief  The provision data callback function
 *
 * @attention If it is WiFi configuration callback function, the data buffer stores WiFi configuration.
 * 
 * @attention If it is initiator information callback function, the data buffer stores initiator information.
 *            Initiator information should be checked in the function.
 *            If the function returns true, responder will send WiFi configuration to initiator.
 *            If the function returns false, responder will not send WiFi configuration to initiator.
 * 
 * @param[in]  src_addr  mac address of sender
 * @param[in]  data  WiFi configuration from responder or initiator information from initiator
 * @param[in]  size  data size
 * @param[in]  rx_ctrl  received packet radio metadata header
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
typedef esp_err_t (*espnow_prov_cb_t)(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);

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
esp_err_t espnow_prov_initiator_scan(espnow_addr_t responder_addr, espnow_prov_responder_t *responder_info, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

/**
 * @brief  Initiator sends information to request WiFi configuration
 *
 * @param[in]  responder_addr  mac address of responder
 * @param[in]  initiator_info  information of initiator
 * @param[in]  cb  WiFi configuration callback function
 * @param[in]  wait_ticks  the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_WIFI_TIMEOUT
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_initiator_send(const espnow_addr_t responder_addr, const espnow_prov_initiator_t *initiator_info, espnow_prov_cb_t cb, TickType_t wait_ticks);

/**
 * @brief  Responder starts to send provision beacon, receives initiator request and send WiFi configuration
 *
 * @param[in]  responder_info  information of responder
 * @param[in]  wait_ticks  provision beacon sending time in ticks
 * @param[in]  wifi_config  WiFi configuration information
 * @param[in]  cb  initiator information callback function
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_prov_responder_start(const espnow_prov_responder_t *responder_info, TickType_t wait_ticks, 
                                        const espnow_prov_wifi_t *wifi_config, espnow_prov_cb_t cb);


#ifdef __cplusplus
}
#endif
