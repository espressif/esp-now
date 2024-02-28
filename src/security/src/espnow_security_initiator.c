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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <protocomm.h>
#include <protocomm_client_security1.h>

#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_security_handshake.h"

static const char* TAG = "espnow_sec_init";

static uint8_t app_key[APP_KEY_LEN] = { 0 };
static bool g_sec_initiator_flag = false;

static size_t g_scan_num = 0;
static espnow_sec_responder_t *g_info_list = NULL;

#ifndef CONFIG_ESPNOW_SEC_SEND_RETRY_NUM
#define CONFIG_ESPNOW_SEC_SEND_RETRY_NUM        1
#endif

#ifndef CONFIG_ESPNOW_SEC_SEND_FORWARD_TTL
#define CONFIG_ESPNOW_SEC_SEND_FORWARD_TTL      0
#endif

#ifndef CONFIG_ESPNOW_SEC_SEND_FORWARD_RSSI
#define CONFIG_ESPNOW_SEC_SEND_FORWARD_RSSI     -65
#endif

static bool addrs_remove(uint8_t addrs_list[][ESPNOW_ADDR_LEN],
                         size_t *addrs_num, const uint8_t addr[6])
{
    if (!addrs_list || !addrs_num || !addr) {
        ESP_LOGE(TAG, "!addrs_list: %p !addrs_num: %p !addr: %p", addrs_list, addrs_num, addr);
        return false;
    }

    for (int i = 0; i < *addrs_num; i++) {
        if (ESPNOW_ADDR_IS_EQUAL(addrs_list[i], addr)) {
            if (--(*addrs_num)) {
                memcpy(addrs_list[i], addrs_list[*addrs_num], ESPNOW_ADDR_LEN);
            }

            return true;
        }
    }

    return false;
}

static esp_err_t espnow_sec_info(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    espnow_sec_info_t *recv_data = (espnow_sec_info_t *)data;

    for (int i = 0; i < g_scan_num; ++i) {
        if (ESPNOW_ADDR_IS_EQUAL((g_info_list)[i].mac, src_addr)) {
            return ESP_OK;
        }
    }

    espnow_addr_t addr_self    = {0};
    esp_wifi_get_mac(ESP_IF_WIFI_STA, addr_self);

    if (recv_data->sec_ver == ESPNOW_SEC_VER_V1_0
        && !memcmp(recv_data->client_mac, addr_self, 6)) {
        ESP_LOGD(TAG, "Device security has been configured by this client, skip.");
        return ESP_OK;
    }

    g_info_list = ESP_REALLOC(g_info_list, (g_scan_num + 1) * sizeof(espnow_sec_responder_t));
    (g_info_list)[g_scan_num].channel = rx_ctrl->channel;
    (g_info_list)[g_scan_num].rssi    = rx_ctrl->rssi;
    memcpy((g_info_list)[g_scan_num].mac, src_addr, 6);
    (g_info_list)[g_scan_num].sec_ver    = recv_data->sec_ver;
    g_scan_num++;

    ESP_LOGD(TAG, "Device information:");
    ESP_LOGD(TAG, "Device channel:   %d", rx_ctrl->channel);
    ESP_LOGD(TAG, "Device rssi:      %d", rx_ctrl->rssi);
    ESP_LOGD(TAG, "Device mac:       " MACSTR "", MAC2STR(src_addr));
    ESP_LOGD(TAG, "Security information:");
    ESP_LOGD(TAG, "Version:          %d", recv_data->sec_ver);
    ESP_LOGD(TAG, "Client MAC:       " MACSTR "", MAC2STR(recv_data->client_mac));

    return ESP_OK;
}

static esp_err_t espnow_sec_initiator_status_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    uint8_t data_type = ((uint8_t *)data)[0];

    switch (data_type) {
        case ESPNOW_SEC_TYPE_INFO:
            ESP_LOGD(TAG, "ESPNOW_SEC_TYPE_INFO");
            ret = espnow_sec_info(src_addr, data, size, rx_ctrl);
            break;

        default:
            break;
    }

    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_ota_handle");
    return ret;
}

esp_err_t espnow_sec_initiator_scan(espnow_sec_responder_t **info_list, size_t *num, TickType_t wait_ticks)
{
    esp_err_t ret = ESP_OK;
    espnow_sec_info_t request_sec_info = {.type = ESPNOW_SEC_TYPE_REQUEST};

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast        = true,
        .magic            = esp_random(),
        .filter_adjacent_channel = true,
        .forward_ttl      = CONFIG_ESPNOW_SEC_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_SEC_SEND_FORWARD_RSSI,
    };

    g_scan_num       = 0;
    ESP_FREE(g_info_list);

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_SECURITY_STATUS, 1, espnow_sec_initiator_status_process);

    for (int i = 0, start_ticks = xTaskGetTickCount(), recv_ticks = 500; i < 5 && wait_ticks - (xTaskGetTickCount() - start_ticks) > 0;
            ++i, recv_ticks = pdMS_TO_TICKS(500)) {
        ret = espnow_send(ESPNOW_DATA_TYPE_SECURITY, ESPNOW_ADDR_BROADCAST, &request_sec_info, 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");

        vTaskDelay(recv_ticks);
    }

    *info_list = g_info_list;
    *num = g_scan_num;

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_SECURITY_STATUS, 0, NULL);

    return ESP_OK;
}

esp_err_t espnow_sec_initiator_scan_result_free(void)
{
    ESP_FREE(g_info_list);
    g_scan_num = 0;

    return ESP_OK;
}

static int addrs_search(espnow_addr_t *addrs_list, size_t addrs_num, espnow_addr_t addr)
{
    if (!addrs_list || !addrs_num || !addr) {
        ESP_LOGE(TAG, "!addrs_list: %p !addrs_num: %d !addr: %p", addrs_list, addrs_num, addr);
        return ESP_FAIL;
    }

    for (int i = 0; i < addrs_num; i++) {
        if (ESPNOW_ADDR_IS_EQUAL(addrs_list[i], addr)) {
            return i;
        }
    }

    return ESP_FAIL;
}

static QueueHandle_t g_sec_queue = NULL;
typedef struct {
    uint8_t src_addr[6];
    void *data;
    size_t size;
} espnow_sec_data_t;

static esp_err_t espnow_initiator_sec_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    if (g_sec_queue) {
        espnow_sec_data_t sec_data = { 0 };
        sec_data.data = ESP_MALLOC(size);
        if (!sec_data.data) {
            return ESP_FAIL;
        }
        memcpy(sec_data.data, data, size);
        sec_data.size = size;
        memcpy(sec_data.src_addr, src_addr, 6);
        if (xQueueSend(g_sec_queue, &sec_data, 0) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send sec queue failed", __func__, __LINE__);
            ESP_FREE(sec_data.data);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t protocomm_espnow_initiator_start(const protocomm_security_t *proto_sec, const void *pop,
                                                const uint8_t addrs_list[][6], size_t addrs_num, espnow_sec_result_t *res)
{
    ESP_PARAM_CHECK(proto_sec);
    ESP_PARAM_CHECK(addrs_list);
    ESP_PARAM_CHECK(addrs_num);

    esp_err_t ret       = ESP_OK;
    uint8_t src_addr[6] = {0};
    espnow_sec_packet_t *req_data = ESP_MALLOC(ESPNOW_DATA_LEN);
    espnow_sec_packet_t *response_data = ESP_MALLOC(ESPNOW_DATA_LEN);
    espnow_sec_result_t *result = ESP_CALLOC(1, sizeof(espnow_sec_result_t));
                         result->successed_addr = ESP_CALLOC(addrs_num, ESPNOW_ADDR_LEN);
    ssize_t  response_size = 0;
    ssize_t  outlen = 0;
    uint8_t *outbuf = NULL;
    int32_t session_id = 0;
    espnow_frame_head_t frame_head = {
        .retransmit_count = CONFIG_ESPNOW_SEC_SEND_RETRY_NUM,
        .filter_adjacent_channel = true,
        .forward_ttl      = CONFIG_ESPNOW_SEC_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_SEC_SEND_FORWARD_RSSI,
    };
    espnow_sec_data_t sec_data = { 0 };

    /* Maximum number of session a time, can be greater but process time will be long*/
    int32_t MAX_NUM = 100;
    int recv_ticks = pdMS_TO_TICKS(100);
    int wait_ticks = 0;
    int start_ticks = 0;
    int retry_count = (addrs_num % MAX_NUM == 0) ? (addrs_num / MAX_NUM + 1) : (addrs_num / MAX_NUM + 2);
    g_sec_initiator_flag = true;

    result->unfinished_num  = addrs_num;
    result->unfinished_addr = ESP_CALLOC(result->unfinished_num, ESPNOW_ADDR_LEN);
    memcpy(result->unfinished_addr, addrs_list, result->unfinished_num * ESPNOW_ADDR_LEN);

    for (int i = 0; i < retry_count && result->unfinished_num > 0 && g_sec_initiator_flag; i++) {
        size_t current_addrs_num = (result->unfinished_num > MAX_NUM) ? MAX_NUM : result->unfinished_num ;
        espnow_addr_t *current_addrs_list = ESP_CALLOC(current_addrs_num, ESPNOW_ADDR_LEN);
        protocomm_security_handle_t *current_session_list = ESP_CALLOC(current_addrs_num, sizeof(protocomm_security_handle_t));
        memcpy(current_addrs_list, result->unfinished_addr, current_addrs_num * ESPNOW_ADDR_LEN);
        size_t success_addrs_num = 0;
        wait_ticks = pdMS_TO_TICKS(1200 + 300 * current_addrs_num);

        ESP_LOGI(TAG, "count: %d, Secure_initiator_send, requested_num: %d, unfinished_num: %d, successed_num: %d",
                 i, current_addrs_num, result->unfinished_num, result->successed_num);

        for (int i = 0; i < current_addrs_num; i++) {
            proto_sec->init(&current_session_list[i]);
            proto_sec->new_transport_session(current_session_list[i], i);
        }

        /**
         * @brief Send Command 0
         */
        espnow_set_group(current_addrs_list, current_addrs_num, ESPNOW_ADDR_GROUP_SEC, NULL, true, portMAX_DELAY);

        ret = write_security1_command0(&outbuf, &outlen);
        if (ret != ESP_OK || !outbuf || !outlen) {
            ESP_LOGW(TAG, "espnow-session cm0 prepare failed");
            ESP_FREE(outbuf);
            goto exit_init;
        }
        frame_head.broadcast = true;
        frame_head.group = true;
        response_data->type = ESPNOW_SEC_TYPE_HANDSHAKE;
        response_data->size = outlen;
        memcpy(response_data->data, outbuf, outlen);
        response_size = sizeof(espnow_sec_packet_t) + outlen;
        ret = espnow_send(ESPNOW_DATA_TYPE_SECURITY, ESPNOW_ADDR_GROUP_SEC, response_data, response_size, &frame_head, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "espnow-session cm0 send failed");
            ESP_FREE(outbuf);
            goto exit_init;
        }
        ESP_FREE(outbuf);
        espnow_set_group(current_addrs_list, current_addrs_num, ESPNOW_ADDR_GROUP_SEC, NULL, false, portMAX_DELAY);

        /**
         * @brief Receive Response 0, send Command 1, receive Response 1
         */
        frame_head.broadcast = false;
        frame_head.group = false;
        start_ticks = xTaskGetTickCount();
        while(xTaskGetTickCount() - start_ticks < wait_ticks && success_addrs_num < current_addrs_num && g_sec_initiator_flag) {
            while (g_sec_queue && (xQueueReceive(g_sec_queue, &sec_data, recv_ticks) == pdPASS)) {
                memcpy(src_addr, sec_data.src_addr, 6);
                memcpy(req_data, sec_data.data, sec_data.size);
                ESP_FREE(sec_data.data);
                session_id = addrs_search(current_addrs_list, current_addrs_num, src_addr);
                ESP_ERROR_CONTINUE(session_id < 0, "addr " MACSTR " not searched", MAC2STR(src_addr));

                if (req_data->type == ESPNOW_SEC_TYPE_KEY_RESP) {
                    ESP_LOGD(TAG, "Session %d successful, mac "MACSTR"", session_id, MAC2STR(src_addr));
                    addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr);
                    memcpy(result->successed_addr[result->successed_num], src_addr, ESPNOW_ADDR_LEN);
                    result->successed_num ++;
                    if (++success_addrs_num == current_addrs_num) {
                        break;
                    }
                } else if (req_data->type == ESPNOW_SEC_TYPE_HANDSHAKE){
                    ret = proto_sec->security_req_handler(current_session_list[session_id], pop, session_id, req_data->data, req_data->size, &outbuf, &outlen, NULL);

                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "espnow-session handler failed");
                        ESP_FREE(outbuf);
                        continue;
                    } else if (outbuf && outlen) {
                        response_data->type = ESPNOW_SEC_TYPE_HANDSHAKE;
                        response_data->size = outlen;
                        memcpy(response_data->data, outbuf, outlen);
                        response_size = sizeof(espnow_sec_packet_t) + outlen;
                        ESP_FREE(outbuf);
                    } else {
                        /* Send APP key */
                        response_data->type = ESPNOW_SEC_TYPE_KEY;
                        if (proto_sec->encrypt) {
                            outlen = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
                            uint8_t *enc_resp = NULL;
                            ret = proto_sec->encrypt(current_session_list[session_id], session_id, app_key, APP_KEY_LEN,
                                                &enc_resp, &outlen);

                            if (ret != ESP_OK) {
                                ESP_LOGE(TAG, "Encryption of data failed for session id %d", session_id);
                                ESP_FREE(enc_resp);
                                continue;
                            }
                            memcpy(response_data->data, enc_resp, outlen);
                            ESP_FREE(enc_resp);
#else
                            ret = proto_sec->encrypt(current_session_list[session_id], session_id, app_key, APP_KEY_LEN,
                                                response_data->data, &outlen);

                            if (ret != ESP_OK) {
                                ESP_LOGE(TAG, "Encryption of data failed for session id %d", session_id);
                                continue;
                            }
#endif
                            response_data->size = outlen;
                            response_size = sizeof(espnow_sec_packet_t) + outlen;
                        } else {/* will not goto here */
                            response_data->size = APP_KEY_LEN;
                            memcpy(response_data->data, app_key, APP_KEY_LEN);
                            response_size = sizeof(espnow_sec_packet_t) + APP_KEY_LEN;
                        }
                    }

                    espnow_add_peer(src_addr, NULL);

                    ret = espnow_send(ESPNOW_DATA_TYPE_SECURITY, src_addr, response_data, response_size, &frame_head, portMAX_DELAY);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "espnow-session send failed");
                    }

                    espnow_del_peer(src_addr);
                }
            }
        }
exit_init:

        for (int i = 0; i < current_addrs_num; i++) {
            proto_sec->close_transport_session(current_session_list[i], i);
            proto_sec->cleanup(current_session_list[i]);
        }

        ESP_FREE(current_addrs_list);
        ESP_FREE(current_session_list);

    }

    if (res) {
        memcpy(res, result, sizeof(espnow_sec_result_t));
    } else {
        espnow_sec_initiator_result_free(result);
    }

    g_sec_initiator_flag = false;

    ESP_FREE(req_data);
    ESP_FREE(response_data);
    ESP_FREE(result);

    return ret;
}

static esp_err_t protocomm_espnow_initiator_stop()
{
    g_sec_initiator_flag = false;

    return ESP_OK;
}

esp_err_t espnow_sec_initiator_start(uint8_t key_info[APP_KEY_LEN], const char *pop_data, const uint8_t addrs_list[][6], size_t addrs_num,
                                    espnow_sec_result_t *res)
{
    ESP_PARAM_CHECK(key_info);
    ESP_PARAM_CHECK(pop_data);
    ESP_PARAM_CHECK(addrs_list);
    ESP_PARAM_CHECK(addrs_num);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    protocomm_security1_params_t pop = {
#else
    protocomm_security_pop_t pop = {
#endif
        .data = (const uint8_t *)pop_data,
        .len  = strlen(pop_data)
    };
    const protocomm_security_t *espnow_sec = &protocomm_client_security1;
    int ret = ESP_OK;

    memcpy(app_key, key_info, APP_KEY_LEN);

    g_sec_queue = xQueueCreate(addrs_num, sizeof(espnow_sec_data_t));
    ESP_ERROR_RETURN(!g_sec_queue, ESP_FAIL, "Create espnow security queue fail");
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_SECURITY, 1, espnow_initiator_sec_process);

    ret = protocomm_espnow_initiator_start(espnow_sec, (const void *)(&pop), addrs_list, addrs_num, res);

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_SECURITY, 0, NULL);
    if (g_sec_queue) {
        espnow_sec_data_t *tmp_data = NULL;

        while (xQueueReceive(g_sec_queue, &tmp_data, 0)) {
            ESP_FREE(tmp_data->data);
        }

        vQueueDelete(g_sec_queue);
        g_sec_queue = NULL;
    }

    return ret;
}

esp_err_t espnow_sec_initiator_stop()
{
    protocomm_espnow_initiator_stop();

    return ESP_OK;
}

esp_err_t espnow_sec_initiator_result_free(espnow_sec_result_t *result)
{
    ESP_PARAM_CHECK(result);

    result->unfinished_num = 0;
    result->requested_num  = 0;
    result->successed_num  = 0;
    ESP_FREE(result->unfinished_addr);
    ESP_FREE(result->requested_addr);
    ESP_FREE(result->successed_addr);

    return ESP_OK;
}
