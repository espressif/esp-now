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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>

#include <mbedtls/aes.h>
#include <mbedtls/ccm.h>

#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_security.h"

static const char* TAG = "espnow_sec";

esp_err_t espnow_sec_init(espnow_sec_t *sec)
{
    ESP_PARAM_CHECK(sec);

    memset(sec, 0, sizeof(espnow_sec_t));
    sec->cipher_ctx = (mbedtls_ccm_context *)ESP_CALLOC(1, sizeof(mbedtls_ccm_context));
    sec->key_len = KEY_LEN;
    sec->iv_len = IV_LEN;
    sec->tag_len = TAG_LEN;

    mbedtls_ccm_init((mbedtls_ccm_context *)sec->cipher_ctx);

    return ESP_OK;
}

esp_err_t espnow_sec_deinit(espnow_sec_t *sec)
{
    ESP_PARAM_CHECK(sec);

    if (sec->cipher_ctx) {
        mbedtls_ccm_free((mbedtls_ccm_context *)sec->cipher_ctx);
        ESP_FREE(sec->cipher_ctx);
    }
    memset(sec, 0, sizeof(espnow_sec_t));

    return ESP_OK;
}

esp_err_t espnow_sec_setkey(espnow_sec_t *sec, uint8_t app_key[APP_KEY_LEN])
{
    ESP_PARAM_CHECK(sec);
    ESP_PARAM_CHECK(app_key);
    ESP_PARAM_CHECK(sec->cipher_ctx);

    int ret = mbedtls_ccm_setkey((mbedtls_ccm_context *)sec->cipher_ctx, MBEDTLS_CIPHER_ID_AES, app_key, 8 * sec->key_len);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "mbedtls_ccm_setkey %x", ret);

    memcpy(sec->key, app_key, sec->key_len);
    memcpy(sec->iv, app_key + sec->key_len, sec->iv_len);
    sec->state = ESPNOW_SEC_OVER;

    return ESP_OK;
}

esp_err_t espnow_sec_send(espnow_sec_t *sec, espnow_type_t type, const uint8_t *dest_addr, const void *data,
                      size_t size, const espnow_frame_head_t *data_head, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(sec);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(size <= ESPNOW_SEC_PACKET_MAX_SIZE);

    /* Following type are not encrypted */
    if (type == ESPNOW_TYPE_ACK || type == ESPNOW_TYPE_FORWARD
            || type == ESPNOW_TYPE_SECURITY_STATUS || type == ESPNOW_TYPE_SECURITY) {
        return espnow_send(type, dest_addr, data, size, data_head, wait_ticks);
    }

    if (sec->state != ESPNOW_SEC_OVER) {
        ESP_LOGE(TAG, "Security state is not over");
        return ESP_FAIL;
    }

    int ret = ESP_OK;
    uint8_t encrypt_len = size + sec->tag_len;
    uint8_t *encrypt_data = ESP_MALLOC(size + sec->tag_len);

    ret = mbedtls_ccm_encrypt_and_tag((mbedtls_ccm_context *)sec->cipher_ctx, size, sec->iv, sec->iv_len, NULL, 0,
                                    data, encrypt_data, encrypt_data + size, sec->tag_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ccm_encrypt_and_tag with error code : %d", ret);
        ESP_FREE(encrypt_data);
        return ESP_FAIL;
    }

    ret = espnow_send(type, dest_addr, encrypt_data, encrypt_len, data_head, wait_ticks);

    ESP_FREE(encrypt_data);

    return ret;
}

esp_err_t espnow_sec_recv(espnow_sec_t *sec, espnow_type_t type,  uint8_t *src_addr, void *data,
                      size_t *size, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(sec);

    int ret = ESP_OK;
    ret = espnow_recv(type, src_addr, data, size, rx_ctrl, wait_ticks);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_recv");

    /* Following types are not encrypted */
    if (type == ESPNOW_TYPE_ACK || type == ESPNOW_TYPE_FORWARD
            || type == ESPNOW_TYPE_SECURITY_STATUS || type == ESPNOW_TYPE_SECURITY) {
        return ret;
    }

    if (sec->state != ESPNOW_SEC_OVER) {
        ESP_LOGE(TAG, "Security state is not over");
        return ESP_FAIL;
    }

    if (*size <= sec->tag_len) {
        ESP_LOGE(TAG, "Data size %d not valid for security", *size);
        return ESP_FAIL;
    }

    *size -= sec->tag_len;
    ret = mbedtls_ccm_auth_decrypt((mbedtls_ccm_context *)sec->cipher_ctx, *size, 
                                        sec->iv, sec->iv_len, NULL, 0,
                                        data, data, data + *size, sec->tag_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ccm_auth_decrypt with error code : %d", ret);
        return ESP_FAIL;
    }

    return ret;
}