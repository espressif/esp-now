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

#include "espnow_utils.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#define APP_KEY_LEN                         32      /**< Exchanged key length */
#define KEY_LEN                             16      /**< Secret key length */
#define IV_LEN                              8       /**< The initialization vector (nonce) length */
#define TAG_LEN                             4       /**< The length of the authentication field */
#define ESPNOW_SEC_PACKET_MAX_SIZE          (ESPNOW_PAYLOAD_LEN - TAG_LEN - IV_LEN)  /**< Maximum length of a single encrypted packet transmitted */

#define ESP_EVENT_ESPNOW_SEC_OK             0x600
#define ESP_EVENT_ESPNOW_SEC_FAIL           0x601

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
typedef struct espnow_sec_s {
    int state;                  /**< State defined by espnow_sec_state_t */
    uint8_t key[KEY_LEN];       /**< Secret key */
    uint8_t iv[IV_LEN];         /**< The initialization vector (nonce) */
    uint8_t key_len;            /**< Secret key length */
    uint8_t iv_len;             /**< The initialization vector (nonce) length */
    uint8_t tag_len;            /**< The length of the authentication field */
    void *cipher_ctx;           /**< The cipher context */
} espnow_sec_t;

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
 * @brief The authenticated encryption function.
 *        Encryption with 128 bit AES-CCM
 *
 * @note  the tag will be appended to the ciphertext
 * 
 * @param[in]   sec        the security info used for encryption.
 * @param[in]   input      the buffer for the input data
 * @param[in]   ilen       the length of the input data
 * @param[out]  output     the buffer for the output data
 * @param[in]   output_len the length of the output buffer in bytes
 * @param[out]  olen       the actual number of bytes written to the output buffer
 * @param[in]   tag_len    the desired length of the authentication tag
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_auth_encrypt(espnow_sec_t *sec, const uint8_t *input, size_t ilen,
                    uint8_t *output, size_t output_len,
                    size_t *olen, size_t tag_len);

/**
 * @brief The authenticated decryption function.
 *        Decryption with 128 bit AES-CCM
 *
 * @note  the tag must be appended to the ciphertext
 * 
 * @param[in]   sec        the security info used for encryption.
 * @param[in]   input      the buffer for the input data
 * @param[in]   ilen       the length of the input data
 * @param[out]  output     the buffer for the output data
 * @param[in]   output_len the length of the output buffer in bytes
 * @param[out]  olen       the actual number of bytes written to the output buffer
 * @param[in]   tag_len    the desired length of the authentication tag
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_auth_decrypt(espnow_sec_t *sec, const uint8_t *input, size_t ilen,
                    uint8_t *output, size_t output_len,
                    size_t *olen, size_t tag_len);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
