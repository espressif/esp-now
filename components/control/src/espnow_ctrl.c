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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "espnow.h"

#include "esp_event_base.h"

#include "esp_utils.h"
#include "esp_storage.h"
#include "esp_mem.h"
#include "espnow_ctrl.h"

#define ESPNOW_BIND_LIST_MAX_SIZE  32
typedef struct {
    int8_t rssi;
    uint32_t timestamp;
    espnow_ctrl_bind_cb_t cb;
    size_t size;
    espnow_ctrl_bind_info_t data[ESPNOW_BIND_LIST_MAX_SIZE];
} espnow_bindlist_t;

static const char *TAG = "espnow_ctrl";
static espnow_bindlist_t g_bindlist = {0};
static TaskHandle_t g_bind_responder_handle = NULL;
static const espnow_frame_head_t g_initiator_frame = {
    .retransmit_count = 10,
    .broadcast        = true,
    .channel          = ESPNOW_CHANNEL_ALL,
    .forward_ttl      = 10,
    .forward_rssi     = -25,
};

static bool espnow_ctrl_responder_is_bindlist(const uint8_t *mac, espnow_attribute_t initiator_attribute)
{
    for (int i = 0; i < g_bindlist.size; ++i) {
        if (!memcmp(g_bindlist.data[i].mac, mac, 6)
                && g_bindlist.data[i].initiator_attribute == initiator_attribute) {
            return true;
        }
    }

    return false;
}

esp_err_t espnow_ctrl_responder_get_bindlist(espnow_ctrl_bind_info_t *list, size_t *size)
{
    if (!list) {
        *size = g_bindlist.size;
    } else {
        *size = MIN(g_bindlist.size, *size);
        memcpy(list, g_bindlist.data, sizeof(espnow_ctrl_bind_info_t) * (*size));
    }

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_set_bindlist(const espnow_ctrl_bind_info_t *info)
{
    if (!espnow_ctrl_responder_is_bindlist(info->mac, info->initiator_attribute)) {
        memcpy(g_bindlist.data + g_bindlist.size, info, sizeof(espnow_ctrl_bind_info_t));
        g_bindlist.size++;
        esp_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
    }

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_remove_bindlist(const espnow_ctrl_bind_info_t *info)
{
    for (int i = 0; i < g_bindlist.size; ++i) {
        if (!memcmp(g_bindlist.data + i, info, sizeof(espnow_ctrl_bind_info_t))) {
            g_bindlist.size--;

            if (g_bindlist.size > 0) {
                g_bindlist.data[g_bindlist.size].initiator_attribute = info->initiator_attribute;
                memcpy(g_bindlist.data[g_bindlist.size].mac, info->mac, 6);
            }

            esp_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
            break;
        }
    }

    return ESP_OK;
}

static void espnow_ctrl_responder_bind_task(void *arg)
{
    esp_err_t ret           = ESP_OK;
    uint8_t src_addr[6]     = {0};
    size_t size            = 0;
    espnow_ctrl_data_t data = {0};
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    espnow_set_qsize(ESPNOW_TYPE_CONTROL_BIND, 8);

    for (;;) {
        ret = espnow_recv(ESPNOW_TYPE_CONTROL_BIND, src_addr, &data, &size, &rx_ctrl, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_recv, ESPNOW_TYPE_CONTROL_BIND");

        if (data.responder_value_b) {
            ESP_LOGD(TAG, "bind, esp_log_timestamp: %d, timestamp: %d, rssi: %d, rssi: %d",
                     esp_log_timestamp(), g_bindlist.timestamp, rx_ctrl.rssi, g_bindlist.rssi);

            bool bind_cb_flag = false;

            if (g_bindlist.cb) {
                bind_cb_flag = g_bindlist.cb(data.initiator_attribute, src_addr, rx_ctrl.rssi);
            }

            if (bind_cb_flag || esp_log_timestamp() < g_bindlist.timestamp || rx_ctrl.rssi >  g_bindlist.rssi) {
                ESP_LOGI("control_func", "addr: "MACSTR", initiator_type: %d, initiator_value: %d",
                         MAC2STR(src_addr), data.initiator_attribute >> 8, data.initiator_attribute & 0xff);

                if (!espnow_ctrl_responder_is_bindlist(src_addr, data.initiator_attribute)) {
                    g_bindlist.data[g_bindlist.size].initiator_attribute = data.initiator_attribute;
                    memcpy(g_bindlist.data[g_bindlist.size].mac, src_addr, 6);

                    esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_BIND,
                                   g_bindlist.data + g_bindlist.size, sizeof(espnow_ctrl_bind_info_t), 0);
                    g_bindlist.size++;
                    esp_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
                }
            }
        } else {
            for (int i = 0; i < g_bindlist.size; ++i) {
                if (!memcmp(g_bindlist.data[i].mac, src_addr, 6)
                        && g_bindlist.data[i].initiator_attribute == data.initiator_attribute) {
                    esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_UNBIND,
                                   g_bindlist.data + i, sizeof(espnow_ctrl_bind_info_t), 0);

                    g_bindlist.size--;

                    if (g_bindlist.size > 0) {
                        g_bindlist.data[g_bindlist.size].initiator_attribute = data.initiator_attribute;
                        memcpy(g_bindlist.data[g_bindlist.size].mac, src_addr, 6);
                    }

                    esp_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
                }
            }
        }
    }

    espnow_set_qsize(ESPNOW_TYPE_CONTROL_BIND, 0);
    g_bind_responder_handle = NULL;

    ESP_LOGW(TAG, "espnow_ctrl_responder_bind_task end");

    vTaskDelete(NULL);
}

esp_err_t espnow_ctrl_responder_bind(uint32_t wait_ms, int8_t rssi, espnow_ctrl_bind_cb_t cb)
{
    if (!g_bind_responder_handle) {
        esp_storage_get("bindlist", &g_bindlist, sizeof(g_bindlist));
        xTaskCreate(espnow_ctrl_responder_bind_task, "espnow_ctrl_responder_bind", 4096,
                    NULL, tskIDLE_PRIORITY + 1, &g_bind_responder_handle);
    }

    g_bindlist.cb        = cb;
    g_bindlist.timestamp = esp_log_timestamp() + wait_ms;
    g_bindlist.rssi      = rssi;

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_recv(espnow_attribute_t *initiator_attribute,
                                     espnow_attribute_t *responder_attribute,
                                     uint32_t *responder_value)
{
    esp_err_t ret           = ESP_OK;
    espnow_ctrl_data_t data = {0};
    size_t size             = 0;
    uint8_t src_addr[6]     = {0};

    espnow_set_qsize(ESPNOW_TYPE_CONTROL_DATA, 8);

    for (;;) {
        ret = espnow_recv(ESPNOW_TYPE_CONTROL_DATA, src_addr, &data, &size, NULL, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_recv", esp_err_to_name(ret));

        ESP_LOGD(TAG, "src_addr: "MACSTR", espnow_ctrl_responder_recv, value: %d",
                 MAC2STR(src_addr), data.responder_value_i);

        if (espnow_ctrl_responder_is_bindlist(src_addr, data.initiator_attribute)) {
            break;
        }
    }

    *initiator_attribute = data.initiator_attribute;
    *responder_attribute = data.responder_attribute;
    *responder_value     = data.responder_value_i;

    return ESP_OK;
}

esp_err_t espnow_ctrl_initiator_bind(espnow_attribute_t initiator_attribute, bool enable)
{
    esp_err_t ret = ESP_OK;
    espnow_ctrl_data_t data = {
        .initiator_attribute = initiator_attribute,
        .responder_value_i  = enable,
    };

    espnow_frame_head_t frame_head = {0};
    memcpy(&frame_head, &g_initiator_frame, sizeof(espnow_frame_head_t));
    frame_head.forward_ttl = 0;

    ret = espnow_send(ESPNOW_TYPE_CONTROL_BIND, ESPNOW_ADDR_BROADCAST, &data,
                      sizeof(espnow_ctrl_data_t), &frame_head, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");

    return ESP_OK;
}

esp_err_t espnow_ctrl_initiator_send(espnow_attribute_t initiator_attribute,
                                     espnow_attribute_t responder_attribute,
                                     uint32_t responder_value)
{
    esp_err_t ret = ESP_OK;
    espnow_ctrl_data_t data = {
        .initiator_attribute = initiator_attribute,
        .responder_attribute = responder_attribute,
        .responder_value_i   = responder_value,
    };

    ret = espnow_send(ESPNOW_TYPE_CONTROL_DATA, ESPNOW_ADDR_BROADCAST, &data, 
                      sizeof(espnow_ctrl_data_t), &g_initiator_frame, pdMS_TO_TICKS(1000));
    ESP_ERROR_RETURN(ret != ESP_OK, ret,  "espnow_broadcast, ret: %d", ret);

    return ESP_OK;
}

esp_err_t espnow_ctrl_send(const espnow_addr_t dest_addr, const espnow_ctrl_data_t *data, const espnow_frame_head_t *frame_head, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(dest_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(frame_head);

    uint8_t size = (!data->responder_value_s_flag && data->responder_value_s_size) ? data->responder_value_s_size : 0;

    esp_err_t ret = espnow_send(ESPNOW_TYPE_CONTROL_DATA, ESPNOW_ADDR_BROADCAST, data, 
                      sizeof(espnow_ctrl_data_t) + size, frame_head, wait_ticks);
    ESP_ERROR_RETURN(ret != ESP_OK, ret,  "espnow_broadcast, ret: %d", ret);

    return ESP_OK;
}

esp_err_t espnow_ctrl_recv(espnow_addr_t src_addr, espnow_ctrl_data_t *data, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    size_t size   = 0;
    espnow_set_qsize(ESPNOW_TYPE_CONTROL_DATA, 8);

    for (;;) {
        ret = espnow_recv(ESPNOW_TYPE_CONTROL_DATA, src_addr, data, &size, rx_ctrl, wait_ticks);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_recv", esp_err_to_name(ret));

        if (espnow_ctrl_responder_is_bindlist(src_addr, data->initiator_attribute)) {
            break;
        }
    }

    return ESP_OK;
}
