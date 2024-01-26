// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "espnow.h"
#include "espnow_utils.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Type of packet
 */
typedef enum {
    ESPNOW_SEC_TYPE_REQUEST,    /**< Request security information */
    ESPNOW_SEC_TYPE_INFO,       /**< Security information */
    ESPNOW_SEC_TYPE_HANDSHAKE,  /**< Handshake packet to get key */
    ESPNOW_SEC_TYPE_KEY,        /**< Packet containing app key */
    ESPNOW_SEC_TYPE_KEY_RESP,   /**< Response to confirm gotten the app key */
    ESPNOW_SEC_TYPE_REST,       /**< Reset security information */
} espnow_sec_type_t;

/**
 * @brief Security version
 */
typedef enum {
    ESPNOW_SEC_VER_NONE,        /**< No security */
    ESPNOW_SEC_VER_V1_0,        /**< The default security version */
    ESPNOW_SEC_VER_V1_1,        /**< Used in the future */
} espnow_sec_ver_type_t;

/**
 * @brief Security information
 */
typedef struct espnow_sec_info_s {
    uint8_t type;               /**< ESPNOW_SEC_TYPE_REQUEST or ESPNOW_SEC_TYPE_INFO */
    uint8_t sec_ver;            /**< Security version */
    uint8_t client_mac[6];      /**< Mac address of initiator */
} espnow_sec_info_t;

/**
 * @brief Responder security information
 */
typedef struct espnow_sec_responder_s {
    uint8_t mac[6];             /**< Mac address of responder */
    int8_t rssi;                /**< Packet rssi */
    uint8_t channel;            /**< The channel of responder */
    uint8_t sec_ver;            /**< The security version of responder */
} espnow_sec_responder_t;

/**
 * @brief Handshake packet
 */
typedef struct espnow_sec_packet_s {
    uint8_t type;               /**< Type of packet, ESPNOW_SEC_TYPE_HANDSHAKE */
    uint8_t size;               /**< Size */
    uint8_t data[0];            /**< Message */
} ESPNOW_PACKED_STRUCT espnow_sec_packet_t;

/**
 * @brief List of device status during the security process
 */
typedef struct espnow_sec_result_s {
    size_t unfinished_num;          /**< The number of devices to be set key */
    espnow_addr_t *unfinished_addr; /**< MAC address of devices to be set key */

    size_t successed_num;           /**< The number of devices that succeeded to set key */
    espnow_addr_t *successed_addr;  /**< MAC address of devices that succeeded to set key */

    size_t requested_num;           /**< Reserved */
    espnow_addr_t *requested_addr;  /**< Reserved */
} espnow_sec_result_t;

/**
 * @brief  Root scans other node info
 *
 * @attention Only called at the root
 *
 * @param[out]  info_list  destination nodes of mac
 * @param[out]  num  number of scaned nodes
 * @param[in]  wait_ticks  the maximum scanning time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_scan(espnow_sec_responder_t **info_list, size_t *num, TickType_t wait_ticks);

/**
 * @brief Free memory in the scan information list
 *
 * @return
 *    - ESP_OK
 */
esp_err_t espnow_sec_initiator_scan_result_free(void);

/**
 * @brief  Root sends security to other nodes
 *
 * @attention Only called at the root
 *
 * @param[in]  key_info  the security key info to sent to responder
 * @param[in]  pop_data  Proof of Possession (PoP) string
 * @param[in]  addrs_list  destination nodes of mac
 * @param[in]  addrs_num  number of destination nodes
 * @param[out]  res  must call espnow_sec_initiator_result_free to free memory
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_start(uint8_t key_info[APP_KEY_LEN], const char *pop_data, const uint8_t addrs_list[][6], size_t addrs_num,
                                    espnow_sec_result_t *res);

/**
 * @brief Stop Root to send security to other nodes
 *
 * @return
 *    - ESP_OK
 */
esp_err_t espnow_sec_initiator_stop();

/**
 * @brief Free memory in the results list
 *
 * @param[in]  result  pointer to device security status
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_result_free(espnow_sec_result_t *result);

/**
 * @brief Start security process
 *
 * @param[in]  pop_data  Proof of Possession (PoP) string
 * 
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_responder_start(const char *pop_data);

/**
 * @brief Stop security process
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_responder_stop();

#ifdef __cplusplus
}
#endif /**< _cplusplus */
