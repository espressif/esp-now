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

#include "esp_utils.h"
#include "espnow.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#define APP_KEY_LEN                 32      /**< Exchanged key length */
#define KEY_LEN                     16      /**< Secret key length */
#define IV_LEN                      8       /**< The initialization vector (nonce) length */
#define TAG_LEN                     4       /**< The length of the authentication field */
#define ESPNOW_SEC_PACKET_MAX_SIZE  (ESPNOW_DATA_LEN - TAG_LEN)  /**< Maximum length of a single encrypted packet transmitted */

/**
 * @brief State of security
 */
typedef enum {
    ESPNOW_SEC_UNFINISHED,      /**< Security handshake is not finished */
    ESPNOW_SEC_OVER,            /**< Security handshake is over and APP key is received */
} espnow_sec_state_t;

/**
 * @brief Struct of security
 */
typedef struct {
    int state;                  /**< State defined by espnow_sec_state_t */
    uint8_t key[KEY_LEN];       /**< Secret key */
    uint8_t iv[IV_LEN];         /**< The initialization vector (nonce) */
    uint8_t key_len;            /**< Secret key length */
    uint8_t iv_len;             /**< The initialization vector (nonce) length */
    uint8_t tag_len;            /**< The length of the authentication field */
    void *cipher_ctx;           /**< The cipher context */
} espnow_sec_t;

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
typedef struct {
    uint8_t type;               /**< ESPNOW_SEC_TYPE_REQUEST or ESPNOW_SEC_TYPE_INFO */
    uint8_t sec_ver;            /**< Security version */
    uint8_t client_mac[6];      /**< Mac address of initiator */
} espnow_sec_info_t;

/**
 * @brief Responder security information
 */
typedef struct {
    uint8_t mac[6];             /**< Mac address of responder */
    int8_t rssi;                /**< Packet rssi */
    uint8_t channel;            /**< The channel of responder */
    uint8_t sec_ver;            /**< The security version of responder */
} espnow_sec_responder_t;

/**
 * @brief Handshake packet
 */
typedef struct {
    uint8_t type;               /**< Type of packet, ESPNOW_SEC_TYPE_HANDSHAKE */
    uint8_t size;               /**< Size */
    uint8_t data[0];            /**< Message */
} __attribute__((packed)) espnow_sec_packet_t;

/**
 * @brief List of device status during the security process
 */
typedef struct {
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
 * @brief  Root sends security to other nodes
 *
 * @attention Only called at the root
 *
 * @param[in]  sec  the security info to record state and key info
 * @param[in]  pop_data  Proof of Possession (PoP) string
 * @param[in]  dest_addrs  destination nodes of mac
 * @param[in]  dest_addrs_num  number of destination nodes
 * @param[out]  result  must call espnow_sec_initator_result_free to free memory
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_start(espnow_sec_t *sec, const char *pop_data, const uint8_t addrs_list[][6], size_t addrs_num,
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
esp_err_t espnow_sec_initator_result_free(espnow_sec_result_t *result);

/**
 * @brief Start security process
 *
 * @param[in]  sec  the security info to record state and key info
 * @param[in]  pop_data  Proof of Possession (PoP) string
 * 
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_responder_start(espnow_sec_t *sec, const char *pop_data);

/**
 * @brief Stop security process
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_responder_stop();

/**
 * @brief Initialize the specified security info
 * 
 * @param[in]  sec  the security info to initialize. This must not be NULL.
 * 
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_init(espnow_sec_t *sec);

/**
 * @brief Clear the specified security info
 * 
 * @param[in]  sec  the security info to clear. This must not be NULL.
 * 
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_deinit(espnow_sec_t *sec);

/**
 * @brief Set the security key info
 * 
 * @param[in]  sec  the security info to set.
 * @param[in]  app_key  raw key info used to set encryption key and iv.
 * 
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_setkey(espnow_sec_t *sec, uint8_t app_key[APP_KEY_LEN]);

/**
 * @brief Encrypt and send ESPNOW data
 *        Encryption with 128 bit AES-CCM
 *
 * @param[in]   sec            the security info to used for encryption.
 * @param[in]   type           ESPNOW data type
 * @param[in]   dest_addr      peer MAC address
 * @param[in]   data           input data to be encrypted and sent
 * @param[in]   size           input data length, the maximum length of data must be no more than ESPNOW_SEC_PACKET_MAX_SIZE
 * @param[in]   data_head      if data_head is NULL, use ESPNOW_FRAME_CONFIG_DEFAULT configuration
 * @param[in]   wait_ticks     the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_send(espnow_sec_t *sec, espnow_type_t type, const uint8_t *dest_addr, const void *data,
                      size_t size, const espnow_frame_head_t *data_head, TickType_t wait_ticks);

/**
 * @brief Recive ESPNOW data and decrypt
 *        Decryption with 128 bit AES-CCM
 *
 * @param[in]  sec  the security info to used for decryption
 * @param[in]  type  ESPNOW data type
 * @param[out]  src_addr  peer MAC address
 * @param[out]  data  output received and decrypted data
 * @param[out]  size  length of output data
 * @param[out]  rx_ctrl  received packet radio metadata header
 * @param[in]  wait_ticks  the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_recv(espnow_sec_t *sec, espnow_type_t type,  uint8_t *src_addr, void *data,
                      size_t *size, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
