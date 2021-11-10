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

#include <protocomm.h>
#include <protocomm_security1.h>

#include "espnow.h"
#include "espnow_security.h"

static const char* TAG = "espnow_sec_resp";

static uint8_t app_key[APP_KEY_LEN] = { 0 };
static protocomm_t *g_espnow_pc = NULL;
static espnow_sec_info_t g_sec_info = { 0 };
static espnow_frame_head_t g_frame_config = { 0 };
static bool g_sec_responder_flag = false;

static esp_err_t espnow_sec_info(const uint8_t *src_addr)
{
    esp_err_t ret = ESP_OK;
    size_t size = sizeof(espnow_sec_info_t);
    espnow_sec_info_t *info = &g_sec_info;

    info->type = ESPNOW_SEC_TYPE_INFO;

    ret = espnow_send(ESPNOW_TYPE_SECURITY_STATUS, src_addr, info, size, &g_frame_config, portMAX_DELAY);

    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    ESP_LOGD(TAG, "Security information:");
    ESP_LOGD(TAG, "Version:          %d", info->sec_ver);
    ESP_LOGD(TAG, "Client MAC:       " MACSTR "", MAC2STR(info->client_mac));

    return ESP_OK;
}

static esp_err_t espnow_sec_reset_info(const uint8_t *src_addr)
{
    espnow_sec_info_t *info = &g_sec_info;
    memset(info, 0, sizeof(espnow_sec_info_t));

    return ESP_OK;
}

static esp_err_t espnow_sec_handle(const char *ep_name, uint8_t resp_type, const espnow_addr_t src_addr, const uint8_t *data, size_t size)
{
    esp_err_t ret       = ESP_OK;
    protocomm_t *pc = g_espnow_pc;
    espnow_sec_packet_t *req_data = (espnow_sec_packet_t *)data;
    espnow_sec_packet_t *response_data = (espnow_sec_packet_t *)ESP_MALLOC(ESPNOW_DATA_LEN);
    ssize_t  response_size = 0;
    ssize_t  outlen = 0;
    uint8_t *outbuf = NULL;
    uint32_t session_id = src_addr[5];
    espnow_frame_head_t frame_head = {
        .retransmit_count = 1,
        .broadcast        = false,
        .filter_adjacent_channel = true,
        .forward_ttl      = 0,
    };

    /* Config or not */
    if (g_sec_info.sec_ver != ESPNOW_SEC_VER_NONE) 
        return ESP_OK;

    /* Message from the same client */
    if (!ESPNOW_ADDR_IS_EMPTY(g_sec_info.client_mac)
        && !ESPNOW_ADDR_IS_EQUAL(src_addr, g_sec_info.client_mac)) {
        return ESP_OK;
    }

    /* Record the client */
    if (ESPNOW_ADDR_IS_EMPTY(g_sec_info.client_mac)) {
        memcpy(g_sec_info.client_mac, src_addr, 6);
        protocomm_open_session(pc, session_id);
    }

    ret = protocomm_req_handle(pc, ep_name, session_id, req_data->data, req_data->size, &outbuf, &outlen);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "espnow-session handler failed");
        memset(g_sec_info.client_mac, 0, 6);
        protocomm_close_session(pc, session_id);
    } else {
        response_data->type = resp_type;
        response_data->size = outlen;
        memcpy(response_data->data, outbuf, outlen);
        response_size = sizeof(espnow_sec_packet_t) + outlen;
        ret = espnow_send(ESPNOW_TYPE_SECURITY, src_addr, response_data, response_size, &frame_head, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "espnow-session send failed");
        }
    }

    ESP_FREE(outbuf);
    ESP_FREE(response_data);

    return ret;
}

static void espnow_sec_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    uint8_t src_addr[6]  = { 0 };
    uint8_t *data   = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t size    = 0;

    g_espnow_pc = arg;
    espnow_set_qsize(ESPNOW_TYPE_SECURITY, 16);
    g_sec_responder_flag = true;

    while (g_sec_responder_flag) {
        ret = espnow_recv(ESPNOW_TYPE_SECURITY, src_addr, data, &size, NULL, pdMS_TO_TICKS(100));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "");

        uint8_t data_type = ((uint8_t *)data)[0];
        espnow_add_peer(src_addr, NULL);

        switch (data_type) {
        case ESPNOW_SEC_TYPE_REQUEST:
            ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_INFO");
            ret = espnow_sec_info(src_addr);
            break;

        case ESPNOW_SEC_TYPE_REST:
            ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_REST");
            ret = espnow_sec_reset_info(src_addr);
            break;

        case ESPNOW_SEC_TYPE_HANDSHAKE:
            ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_HANDSHAKE");
            ret = espnow_sec_handle("espnow-session", ESPNOW_SEC_TYPE_HANDSHAKE, src_addr, data, size);
            break;

        case ESPNOW_SEC_TYPE_KEY:
            ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_KEY");
            ret = espnow_sec_handle("espnow-config", ESPNOW_SEC_TYPE_KEY_RESP, src_addr, data, size);
            break;

        default:
            break;
        }

        espnow_del_peer(src_addr);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_sec_handle");
    }

    espnow_set_qsize(ESPNOW_TYPE_SECURITY, 0);
    g_sec_responder_flag = false;

    ESP_FREE(data);
    vTaskDelete(NULL);
}

static esp_err_t protocomm_espnow_responder_start(protocomm_t *pc)
{
    xTaskCreate(espnow_sec_task, "espnow_sec", 3 * 1024, pc, tskIDLE_PRIORITY + 1, NULL);

    return ESP_OK;
}

static esp_err_t espnow_config_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    ESP_PARAM_CHECK(inlen >= APP_KEY_LEN);

    memcpy(app_key, inbuf, APP_KEY_LEN);
    /* Mark as configured */
    g_sec_info.sec_ver = ESPNOW_SEC_VER_V1_0;

    /* return the origin message */
    *outlen = inlen;
    *outbuf = (uint8_t *) ESP_MALLOC(*outlen);
    if (!*outbuf) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    memcpy(*outbuf, inbuf, inlen);

    ESP_LOGI(TAG, "Get APP key");

    if (priv_data) {
        espnow_sec_setkey((espnow_sec_t *)priv_data, app_key);
    }

    return ESP_OK;
}

static esp_err_t protocomm_espnow_responder_stop()
{
    g_sec_responder_flag = false;

    return ESP_OK;
}

esp_err_t espnow_sec_responder_start(espnow_sec_t *sec, const char *pop_data)
{
    ESP_PARAM_CHECK(sec);
    ESP_PARAM_CHECK(pop_data);

    if (g_espnow_pc) {
        ESP_LOGW(TAG, "Have create the new protocomm instance");
        return ESP_FAIL;
    }

    esp_err_t ret;
    protocomm_security_pop_t pop = {
        .data = (const uint8_t *)pop_data,
        .len  = strlen(pop_data)
    };
    protocomm_t *pc = protocomm_new();
    if (pc == NULL) {
        ESP_LOGE(TAG, "Failed to create new protocomm instance");
        return ESP_FAIL;
    }

    /* Set version */
    ret = protocomm_set_version(pc, "espnow-ver", "v0.1");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set version endpoint");
        protocomm_delete(pc);
        return ret;
    }

    /* Set protocomm security type for endpoint */
    ret = protocomm_set_security(pc, "espnow-session", &protocomm_security1, &pop);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set security endpoint");
        protocomm_delete(pc);
        return ret;
    }

    /* Add protocomm endpoint for security key */
    ret = protocomm_add_endpoint(pc, "espnow-config", espnow_config_data_handler, sec);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set security key endpoint");
        protocomm_delete(pc);
        return ret;
    }

    protocomm_espnow_responder_start(pc);

    return ESP_OK;
}

esp_err_t espnow_sec_responder_stop()
{
    protocomm_espnow_responder_stop();

    if (g_espnow_pc) {
        protocomm_delete(g_espnow_pc);
        g_espnow_pc = NULL;
    }

    return ESP_OK;
}
