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

#define APP_KEY_LEN                 32
#define KEY_LEN                     16
#define IV_LEN                      8
#define TAG_LEN                     4
#define ESPNOW_SEC_PACKET_MAX_SIZE  (ESPNOW_DATA_LEN - TAG_LEN)  /**< Maximum length of a single packet transmitted */

/**
 * @brief State of security
 */
typedef enum {
    ESPNOW_SEC_UNFINISHED,
    ESPNOW_SEC_OVER,
} espnow_sec_state_t;

/**
 * @brief Struct of security
 */
typedef struct {
    int state;
    uint8_t key[KEY_LEN];
    uint8_t iv[IV_LEN];
    uint8_t key_len;
    uint8_t iv_len;
    uint8_t tag_len;
    void *cipher_ctx;
} espnow_sec_t;

/**
 * @brief Type of packet
 */
typedef enum {
    ESPNOW_SEC_TYPE_REQUEST,
    ESPNOW_SEC_TYPE_INFO,
    ESPNOW_SEC_TYPE_HANDSHAKE,
    ESPNOW_SEC_TYPE_KEY,
    ESPNOW_SEC_TYPE_KEY_RESP,
    ESPNOW_SEC_TYPE_REST,
} espnow_sec_type_t;

typedef enum {
    ESPNOW_SEC_VER_NONE,
    ESPNOW_SEC_VER_V1_0,
    ESPNOW_SEC_VER_V1_1,
} espnow_sec_ver_type_t;

typedef struct {
    uint8_t type;
    uint8_t sec_ver;
    uint8_t client_mac[6];
} espnow_sec_info_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t sec_ver;
} espnow_sec_responder_t;

/**
 * @brief Handshake packet
 */
typedef struct {
    uint8_t type;  /**< Type of packet, ESPNOW_SEC_TYPE_HANDSHAKE */
    uint8_t size;  /**< Size */
    uint8_t data[0]; /**< Message */
} __attribute__((packed)) espnow_sec_packet_t;

/**
 * @brief List of devices' status during the security process
 */
typedef struct {
    size_t unfinished_num;    /**< The number of devices to be set key */
    espnow_addr_t *unfinished_addr; /**< MAC address of devices to be set key */

    size_t successed_num;     /**< The number of devices that succeeded to set key */
    espnow_addr_t *successed_addr;  /**< MAC address of devices that succeeded to set key */

    size_t requested_num;     /**< The number of devices to be set key */
    espnow_addr_t *requested_addr;  /**< This address is used to buffer the result of the request during the set key process */
} espnow_sec_result_t;

/**
 * @brief  Root scans other node info
 *
 * @attention Only called at the root
 *
 * @param  info_list      Destination nodes of mac
 * @param  num            Number of scaned nodes
 * @param  wait_ticks     Scan time in ticks
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
 * @param  sec            The Security info to record state and key info
 * @param  pop_data       Proof of Possession (PoP) string
 * @param  dest_addrs     Destination nodes of mac
 * @param  dest_addrs_num Number of destination nodes
 * @param  result         Must call espnow_sec_initator_result_free to free memory
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
 * @brief  Free memory in the results list
 *
 * @param  result Pointer to device security status
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initator_result_free(espnow_sec_result_t *result);

/**
 * @brief Start security process
 *
 * @param  sec            The Security info to record state and key info
 * @param  pop_data       Proof of Possession (PoP) string
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
 * @param  sec            The Security info to initialize. This must not be NULL.
 * 
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_init(espnow_sec_t *sec);

/**
 * @brief Clear the specified security info
 * 
 * @param  sec            The Security info to clear. This must not be NULL.
 * 
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_deinit(espnow_sec_t *sec);

/**
 * @brief Set the security key info
 * 
 * @param  sec            The Security info to set.
 * @param  app_key        Raw key info to use to set encryption key and iv.
 * 
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_setkey(espnow_sec_t *sec, uint8_t app_key[APP_KEY_LEN]);

/**
 * @brief   Encrypt and send ESPNOW data
 *          Encryption with 128 bit AES-CCM
 *
 * @param   sec            The Security info to used for encryption.
 * @param   type           ESPNOW data type
 * @param   dest_addr      Peer MAC address
 * @param   data           Input data to be encrypted and sent
 * @param   size           Input data length, the maximum length of data must be less than ESPNOW_SEC_PACKET_MAX_SIZE
 * @param   data_head      If data_head is NULL, Use ESPNOW_FRAME_CONFIG_DEFAULT configuration
 * @param   wait_ticks     Send time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_send(espnow_sec_t *sec, espnow_type_t type, const uint8_t *dest_addr, const void *data,
                      size_t size, const espnow_frame_head_t *data_head, TickType_t wait_ticks);

/**
 * @brief   Recive ESPNOW data and decrypt
 *          Decryption with 128 bit AES-CCM
 *
 * @param   sec            The Security info to used for decryption.
 * @param   type           ESPNOW data type
 * @param   src_addr       Peer MAC address
 * @param   data           Output received and decrypted data
 * @param   size           Length of output data
 * @param   rx_ctrl        Received packet radio metadata header
 * @param   wait_ticks     Recive time in ticks
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
