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

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_wifi.h"

#include "esp_utils.h"
#include "espnow.h"
#include "espnow_prov.h"

typedef enum {
    ESPNOW_PROV_TYPE_BEACON,
    ESPNOW_PROV_TYPE_DEVICE,
    ESPNOW_PROV_TYPE_WIFI,
} espnow_prov_type_t;

typedef struct {
    uint8_t type;
    union {
        espnow_prov_initator_t initator_info;
        espnow_prov_responder_t responder_info;
        espnow_prov_wifi_t wifi_config;
    };
} __attribute__((packed)) espnow_prov_data_t;

#define ESPNOW_PROV_BEACON_INTERVAL 100

static const char *TAG = "espnow_prov";

esp_err_t espnow_prov_initator_scan(espnow_addr_t responder_addr, espnow_prov_responder_t *responder_info,
                                    wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_info);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret                 = ESP_OK;
    espnow_prov_data_t *recv_data = ESP_MALLOC(sizeof(espnow_prov_data_t));

    wifi_country_t country = {0};
    size_t recv_size = 0;
    uint32_t start_ticks = xTaskGetTickCount();

    espnow_set_qsize(ESPNOW_TYPE_PROV, 8);
    esp_wifi_get_country(&country);

    while (wait_ticks == portMAX_DELAY || xTaskGetTickCount() - start_ticks < wait_ticks) {
        for (int i = 0; i < country.nchan; ++i) {
            esp_wifi_set_channel(country.schan + i, WIFI_SECOND_CHAN_NONE);
            ESP_LOGD(TAG, "espnow_send, channel: %d", country.schan + i);

            while (espnow_recv(ESPNOW_TYPE_PROV, responder_addr, recv_data,
                               &recv_size, rx_ctrl, pdMS_TO_TICKS(ESPNOW_PROV_BEACON_INTERVAL + 10)) == ESP_OK) {
                ESP_ERROR_CONTINUE(recv_data->type != ESPNOW_PROV_TYPE_BEACON, "");
                memcpy(responder_info, &recv_data->responder_info, sizeof(espnow_prov_responder_t));

                ret = ESP_OK;
                goto EXIT;
            }
        }
    }

EXIT:
    ESP_FREE(recv_data);

    return ret;
}

esp_err_t espnow_prov_initator_send(const espnow_addr_t responder_addr, const espnow_prov_initator_t *initator_info)
{
    ESP_PARAM_CHECK(responder_addr);
    ESP_PARAM_CHECK(initator_info);

    esp_err_t ret = ESP_ERR_WIFI_TIMEOUT;
    espnow_prov_data_t *prov_data = ESP_MALLOC(sizeof(espnow_prov_data_t));
    prov_data->type = ESPNOW_PROV_TYPE_DEVICE;
    memcpy(&prov_data->initator_info, initator_info, sizeof(espnow_prov_initator_t));
    espnow_frame_head_t frame_head = {
        .filter_adjacent_channel = true,
    };

    espnow_add_peer(responder_addr, NULL);

    ret = espnow_send(ESPNOW_TYPE_PROV, responder_addr, prov_data,
                      sizeof(espnow_prov_data_t), &frame_head, portMAX_DELAY);

    espnow_del_peer(responder_addr);
    ESP_FREE(prov_data);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    return ESP_OK;
}

esp_err_t espnow_prov_initator_recv(espnow_addr_t responder_addr, espnow_prov_wifi_t *wifi_config, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_addr);
    ESP_PARAM_CHECK(wifi_config);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret                 = ESP_OK;
    uint32_t start_ticks          = xTaskGetTickCount();
    espnow_prov_data_t *recv_data = ESP_MALLOC(sizeof(espnow_prov_data_t));
    size_t recv_size = 0;

    while (wait_ticks == portMAX_DELAY || xTaskGetTickCount() - start_ticks < wait_ticks) {
        ret = espnow_recv(ESPNOW_TYPE_PROV, responder_addr, recv_data, &recv_size, rx_ctrl, wait_ticks);
        ESP_ERROR_BREAK(ret != ESP_OK, "<%s> espnow_recv", esp_err_to_name(ret));
        ESP_ERROR_CONTINUE(recv_data->type != ESPNOW_PROV_TYPE_WIFI, "recv_data->type != ESPNOW_PROV_TYPE_WIFI: %d", recv_data->type);
        memcpy(wifi_config, &recv_data->wifi_config, sizeof(espnow_prov_wifi_t));
        break;
    }

    ESP_FREE(recv_data);
    return ret;
}

static uint32_t g_beacon_stop_tick = 0;
static espnow_prov_data_t *g_beacon_prov_data = NULL;

static void responder_beacon_timercb(void *timer)
{
    if (g_beacon_stop_tick < xTaskGetTickCount()) {
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
        ESP_FREE(g_beacon_prov_data);
        ESP_LOGI(TAG, "Responder beacon end");
        return ;
    }

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
    };

    espnow_send(ESPNOW_TYPE_PROV, ESPNOW_ADDR_BROADCAST, g_beacon_prov_data, 
                sizeof(espnow_prov_responder_t) + 1, &frame_head, 0);
}

esp_err_t espnow_prov_responder_beacon(const espnow_prov_responder_t *responder_info, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_info);

    if (!g_beacon_prov_data) {
        g_beacon_prov_data = ESP_MALLOC(sizeof(espnow_prov_responder_t) + 1);
        TimerHandle_t timer = xTimerCreate("responder_beacon",
                                           pdMS_TO_TICKS(ESPNOW_PROV_BEACON_INTERVAL),
                                           true, NULL, responder_beacon_timercb);
        xTimerStart(timer, 0);
    }

    g_beacon_stop_tick = xTaskGetTickCount() + wait_ticks;
    g_beacon_prov_data->type = ESPNOW_PROV_TYPE_BEACON;
    memcpy(&g_beacon_prov_data->responder_info, responder_info, sizeof(espnow_prov_responder_t));
    ESP_LOGI(TAG, "Responder beacon start, timer: %ds", pdTICKS_TO_MS(wait_ticks) / 1000);

    return ESP_OK;
}

esp_err_t espnow_prov_responder_recv(espnow_addr_t initator_addr, espnow_prov_initator_t *initator_info,
                                     wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(initator_info);
    ESP_PARAM_CHECK(rx_ctrl);
    ESP_PARAM_CHECK(wait_ticks != portMAX_DELAY);

    esp_err_t ret     = ESP_OK;
    size_t recv_size = 0;
    espnow_prov_data_t *recv_data = ESP_MALLOC(ESPNOW_DATA_LEN);

    espnow_set_qsize(ESPNOW_TYPE_PROV, 8);

    for (int32_t start_ticks = xTaskGetTickCount(), recv_ticks = wait_ticks; recv_ticks > 0;
            recv_ticks -= (xTaskGetTickCount() - start_ticks)) {
        ret = espnow_recv(ESPNOW_TYPE_PROV, initator_addr, recv_data, &recv_size, rx_ctrl, recv_ticks);
        ESP_ERROR_BREAK(ret == ESP_ERR_TIMEOUT, "");
        ESP_ERROR_CONTINUE(recv_data->type != ESPNOW_PROV_TYPE_DEVICE,
                           MACSTR ", type: %d", MAC2STR(initator_addr), recv_data->type);
        memcpy(initator_info, &recv_data->initator_info, sizeof(espnow_prov_initator_t) + recv_data->initator_info.custom_size);
        break;
    }

    ESP_FREE(recv_data);

    return ret;
}

esp_err_t espnow_prov_responder_send(const espnow_addr_t *initator_addr_list, size_t initator_addr_num,
                                     const espnow_prov_wifi_t *wifi_config)
{
    ESP_PARAM_CHECK(initator_addr_list);
    ESP_PARAM_CHECK(wifi_config);

    esp_err_t ret = ESP_OK;
    uint8_t dest_addr[6] = {0};
    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
    };

    ESP_LOGD(TAG, MACSTR ", num: %d", MAC2STR(initator_addr_list[0]), initator_addr_num);

    if (initator_addr_num > 1) {
        espnow_send_group(initator_addr_list, initator_addr_num, ESPNOW_ADDR_GROUP_PROV, NULL, true, portMAX_DELAY);
        frame_head.group = true;
        memcpy(dest_addr, ESPNOW_ADDR_GROUP_PROV, 6);
    } else {
        memcpy(dest_addr, initator_addr_list[0], 6);
        espnow_add_peer(dest_addr, NULL);
    }

    size_t size = 1 + sizeof(espnow_prov_wifi_t) + wifi_config->custom_size;
    espnow_prov_data_t *prov_data = ESP_MALLOC(size);
    memcpy(&prov_data->wifi_config, wifi_config, size - 1);
    prov_data->type = ESPNOW_PROV_TYPE_WIFI;

    ret = espnow_send(ESPNOW_TYPE_PROV, dest_addr, prov_data, size, &frame_head, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");

    ESP_FREE(prov_data);

    if (initator_addr_num > 1) {
        espnow_send_group(initator_addr_list, initator_addr_num, ESPNOW_ADDR_GROUP_PROV, NULL, false, portMAX_DELAY);
    } else {
        espnow_del_peer(dest_addr);
    }

    return ESP_OK;
}
