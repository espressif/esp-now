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
#include "esp_system.h"
#include "esp_event.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include <protocomm.h>
#include <protocomm_security1.h>

#include "espnow.h"
#include "espnow_security_handshake.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "espnow_sec_resp";

static uint8_t app_key[APP_KEY_LEN] = { 0 };
static protocomm_t *g_espnow_pc = NULL;
static espnow_sec_info_t g_sec_info = { 0 };
static espnow_frame_head_t g_frame_config = { 0 };
static uint32_t g_sec_session_start = 0;    /* start (ms) of the current half-open handshake */
/* Set by the espnow-config endpoint once the app key is installed; espnow_sec_handle()
 * turns it into sec_ver = V1_0 only after the KEY_RESP has been produced and sent. */
static bool g_sec_key_installed = false;
/* Serializes espnow_sec_responder_start() / _stop() with the SECURITY frame handler, which
 * runs in the espnow task: stop() waits for an in-flight request (including the encryption
 * and sending of its response) to finish before protocomm is deleted. */
static SemaphoreHandle_t g_sec_lock = NULL;

#ifndef CONFIG_ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS
#define CONFIG_ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS  (10 * 1000)
#endif
#define ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS         CONFIG_ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS

static esp_err_t espnow_sec_info(const uint8_t *src_addr)
{
    esp_err_t ret = ESP_OK;
    size_t size = sizeof(espnow_sec_info_t);
    espnow_sec_info_t *info = &g_sec_info;

    info->type = ESPNOW_SEC_TYPE_INFO;

    ret = espnow_send(ESPNOW_DATA_TYPE_SECURITY_STATUS, src_addr, info, size, &g_frame_config, portMAX_DELAY);

    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    ESP_LOGD(TAG, "Security information:");
    ESP_LOGD(TAG, "Version:          %d", info->sec_ver);
    ESP_LOGD(TAG, "Client MAC:       " MACSTR "", MAC2STR(info->client_mac));

    return ESP_OK;
}

#if !CONFIG_ESPNOW_SEC_IGNORE_REST
static esp_err_t espnow_sec_reset_info(const uint8_t *src_addr)
{
    espnow_sec_info_t *info = &g_sec_info;
    memset(info, 0, sizeof(espnow_sec_info_t));

    return ESP_OK;
}
#endif

static esp_err_t espnow_sec_handle(const char *ep_name, uint8_t resp_type, const espnow_addr_t src_addr, const uint8_t *data, size_t size)
{
    esp_err_t ret       = ESP_OK;
    protocomm_t *pc = g_espnow_pc;
    espnow_sec_packet_t *req_data = (espnow_sec_packet_t *)data;
    espnow_sec_packet_t *response_data = NULL;  /* allocated only once a response is due */
    bool key_received = false;
    bool pc_failed = false;
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

    /* req_data->size is passed to protocomm as the payload length; it must fit the frame. */
    if (size < sizeof(espnow_sec_packet_t)
            || req_data->size > size - sizeof(espnow_sec_packet_t)) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Drop malformed SECURITY frame: size=%u req_size=%u",
                                (unsigned)size, (unsigned)((size >= sizeof(espnow_sec_packet_t)) ? req_data->size : 0));
        return ESP_ERR_INVALID_ARG;
    }

    /* Config or not */
    if (g_sec_info.sec_ver != ESPNOW_SEC_VER_NONE) 
        return ESP_OK;

    /* Message from the same client */
    if (!ESPNOW_ADDR_IS_EMPTY(g_sec_info.client_mac)
        && !ESPNOW_ADDR_IS_EQUAL(src_addr, g_sec_info.client_mac)) {
        /* Reclaim the single client slot once a half-open handshake times out. */
        if (esp_log_timestamp() - g_sec_session_start < ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Reclaiming stale half-open handshake slot from " MACSTR,
                 MAC2STR(g_sec_info.client_mac));
        protocomm_close_session(pc, g_sec_info.client_mac[5]);
        memset(g_sec_info.client_mac, 0, 6);
    }

    /* Record the client */
    if (ESPNOW_ADDR_IS_EMPTY(g_sec_info.client_mac)) {
        ret = protocomm_open_session(pc, session_id);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "protocomm_open_session failed: %s", esp_err_to_name(ret));
            return ret;
        }
        memcpy(g_sec_info.client_mac, src_addr, 6);
        g_sec_session_start = esp_log_timestamp();
    }

    g_sec_key_installed = false;
    ret = protocomm_req_handle(pc, ep_name, session_id, req_data->data, req_data->size, &outbuf, &outlen);
    /* The espnow-config endpoint sets this before protocomm encrypts the response,
     * so it may be set although protocomm_req_handle() failed. */
    key_received = g_sec_key_installed;
    g_sec_key_installed = false;
    if (ret != ESP_OK) {
        pc_failed = true;
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGE, 1000, "espnow-session handler failed for " MACSTR ": %s",
                                MAC2STR(src_addr), esp_err_to_name(ret));
    } else {
        /* The endpoint echoes client data; bound outlen to the fixed response buffer. */
        if (outlen < 0 || (size_t)outlen > ESPNOW_DATA_LEN - sizeof(espnow_sec_packet_t)) {
            ESP_LOGW(TAG, "Drop oversized session response: outlen=%d (max %u)",
                     (int)outlen, (unsigned)(ESPNOW_DATA_LEN - sizeof(espnow_sec_packet_t)));
            ret = ESP_ERR_INVALID_SIZE;
        } else if (!(response_data = (espnow_sec_packet_t *)ESP_MALLOC(ESPNOW_DATA_LEN))) {
            ESP_LOGW(TAG, "Not enough memory for response");
            ret = ESP_ERR_NO_MEM;
        } else {
            response_data->type = resp_type;
            response_data->size = outlen;
            memcpy(response_data->data, outbuf, outlen);
            response_size = sizeof(espnow_sec_packet_t) + outlen;
            ret = espnow_send(ESPNOW_DATA_TYPE_SECURITY, src_addr, response_data, response_size, &frame_head, portMAX_DELAY);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "espnow-session send failed");
            }
        }
    }

    ESP_FREE(outbuf);
    ESP_FREE(response_data);

    if (pc_failed || (key_received && ret != ESP_OK)) {
        /* Either protocomm failed (handshake step or config step: decrypt, endpoint
         * or encryption of the response) or the KEY_RESP could not be built or
         * sent. Nothing is committed: sec_ver stays NONE and the client slot is
         * released, so the same initiator can simply run the handshake again. If
         * the endpoint had already installed the app key it stays installed -
         * exactly the state a KEY_RESP lost on the air leaves behind - and the
         * retry installs the key it carries. */
        protocomm_close_session(pc, session_id);
        if (key_received) {
            ESP_LOGW(TAG, "KEY_RESP for " MACSTR " not sent (%s), handshake left open for retry",
                     MAC2STR(src_addr), esp_err_to_name(ret));
        }
        /* Posted after the last use of `pc`: the handler may stop the responder. */
        esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_SEC_FAIL, g_sec_info.client_mac, sizeof(espnow_addr_t), 0);
        memset(g_sec_info.client_mac, 0, 6);
    } else if (key_received) {
        /* Commit: the KEY_RESP has been encrypted by protocomm and handed to
         * espnow_send(). From here on further handshakes are ignored.
         * ESP_EVENT_ESPNOW_SEC_OK is posted only here: a handler that calls
         * espnow_sec_responder_stop() on this event does not pull protocomm away
         * from a request that is still being answered. */
        g_sec_info.sec_ver = ESPNOW_SEC_VER_V1_0;
        esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_SEC_OK, g_sec_info.client_mac, sizeof(espnow_addr_t), 0);
    }

    return ret;
}

static esp_err_t espnow_sec_responder_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    uint8_t data_type = ((uint8_t *)data)[0];

    xSemaphoreTake(g_sec_lock, portMAX_DELAY);
    if (!g_espnow_pc) {
        /* Stopped after this frame was queued. */
        xSemaphoreGive(g_sec_lock);
        return ESP_OK;
    }

    /* Only remove the peer if this handler added it: src_addr is forgeable, so a
     * frame naming an existing long-lived peer must not delete it on cleanup. */
    bool peer_existed = esp_now_is_peer_exist(src_addr);
    espnow_add_peer(src_addr, NULL);

    switch (data_type) {
    case ESPNOW_SEC_TYPE_REQUEST:
        ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_INFO");
        ret = espnow_sec_info(src_addr);
        break;

    case ESPNOW_SEC_TYPE_REST:
        /* REST is plaintext and its source address can be forged, so anyone in
         * range can force a new handshake. Honoured by default for compatibility;
         * see CONFIG_ESPNOW_SEC_IGNORE_REST. Clear the state locally via
         * espnow_sec_responder_stop() / espnow_erase_key() instead. */
#if CONFIG_ESPNOW_SEC_IGNORE_REST
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Ignoring unauthenticated REST from " MACSTR, MAC2STR(src_addr));
#else
        ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_REST from " MACSTR, MAC2STR(src_addr));
        ret = espnow_sec_reset_info(src_addr);
#endif
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

    if (!peer_existed) {
        espnow_del_peer(src_addr);
    }
    xSemaphoreGive(g_sec_lock);
    /* Already logged (rate-limited) in espnow_sec_handle. */
    return ret;
}

static esp_err_t protocomm_espnow_responder_start(protocomm_t *pc)
{
    g_espnow_pc = pc;
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_SECURITY, 1, espnow_sec_responder_process);

    return ESP_OK;
}

static esp_err_t espnow_config_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                        uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    esp_err_t ret = ESP_OK;

    ESP_PARAM_CHECK(inlen >= APP_KEY_LEN);

    memcpy(app_key, inbuf, APP_KEY_LEN);

    /* return the origin message */
    *outlen = inlen;
    *outbuf = (uint8_t *) ESP_MALLOC(*outlen);
    if (!*outbuf) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    memcpy(*outbuf, inbuf, inlen);

    ESP_LOGI(TAG, "Get APP key");

    ret = espnow_set_key(app_key);
    ret |= espnow_set_dec_key(app_key);
    if (ret != ESP_OK) {
        /* Not configured; protocomm_req_handle() fails and espnow_sec_handle()
         * posts ESP_EVENT_ESPNOW_SEC_FAIL once it is done with the request. */
        ESP_FREE(*outbuf);
        *outlen = 0;
        return ret;
    }

    /* The key is installed, but protocomm still has to encrypt this response and
     * espnow_sec_handle() has to send it; only then is the responder marked as
     * configured (sec_ver) and ESP_EVENT_ESPNOW_SEC_OK posted. Marking it here
     * would leave a responder that ignores every further handshake if either
     * step fails. */
    g_sec_key_installed = true;

    return ESP_OK;
}

static esp_err_t protocomm_espnow_responder_stop()
{
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_SECURITY, 0, NULL);

    return ESP_OK;
}

esp_err_t espnow_sec_responder_start(const char *pop_data)
{
    ESP_PARAM_CHECK(pop_data);

    if (g_espnow_pc) {
        ESP_LOGW(TAG, "Have create the new protocomm instance");
        return ESP_FAIL;
    }

    esp_err_t ret;
    if (!g_sec_lock) {
        g_sec_lock = xSemaphoreCreateMutex();
        ESP_ERROR_RETURN(!g_sec_lock, ESP_ERR_NO_MEM, "xSemaphoreCreateMutex");
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    protocomm_security1_params_t pop = {
#else
    protocomm_security_pop_t pop = {
#endif
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
    ret = protocomm_add_endpoint(pc, "espnow-config", espnow_config_data_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set security key endpoint");
        protocomm_delete(pc);
        return ret;
    }

    xSemaphoreTake(g_sec_lock, portMAX_DELAY);
    protocomm_espnow_responder_start(pc);
    xSemaphoreGive(g_sec_lock);

    return ESP_OK;
}

esp_err_t espnow_sec_responder_stop()
{
    protocomm_espnow_responder_stop();

    /* Wait for a SECURITY frame that is being handled in the espnow task (the
     * handler may be answering it through protocomm right now, e.g. from a
     * ESP_EVENT_ESPNOW_SEC_OK handler) before deleting protocomm. */
    if (g_sec_lock) {
        xSemaphoreTake(g_sec_lock, portMAX_DELAY);
    }

    if (g_espnow_pc) {
        protocomm_delete(g_espnow_pc);
        g_espnow_pc = NULL;
    }

    memset(&g_sec_info, 0, sizeof(espnow_sec_info_t));
    memset(&g_frame_config, 0, sizeof(espnow_frame_head_t));
    memset(app_key, 0, APP_KEY_LEN);

    if (g_sec_lock) {
        xSemaphoreGive(g_sec_lock);
    }

    return ESP_OK;
}
