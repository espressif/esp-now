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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "espnow.h"
#include "espnow_prov.h"
#include "espnow_utils.h"

typedef enum {
    ESPNOW_PROV_TYPE_BEACON,
    ESPNOW_PROV_TYPE_DEVICE,
    ESPNOW_PROV_TYPE_WIFI,
} espnow_prov_type_t;

typedef struct {
    uint8_t type;
    union {
        espnow_prov_initiator_t initiator_info;
        espnow_prov_responder_t responder_info;
        espnow_prov_wifi_t wifi_config;
    };
} __attribute__((packed)) espnow_prov_data_t;

#define ESPNOW_PROV_BEACON_INTERVAL 100

#ifdef CONFIG_ESPNOW_ALL_SECURITY
#define CONFIG_ESPNOW_PROV_SECURITY 1
#else
#ifndef CONFIG_ESPNOW_PROV_SECURITY
#define CONFIG_ESPNOW_PROV_SECURITY 0
#endif
#endif

static const char *TAG = "espnow_prov";

typedef struct {
    bool beacon_en;
    bool wifi_en;
    bool fix_ch;
    bool config;
    espnow_prov_cb_t wifi_cb;
    uint32_t stop_tick;
    struct {
        espnow_prov_responder_t *responder_info;
        wifi_pkt_rx_ctrl_t *rx_ctrl;
        uint8_t *addr;
    } scan_info;
} espnow_prov_init_t;

typedef struct {
    bool device_en;
    espnow_prov_cb_t device_cb;
    espnow_prov_wifi_t *wifi_config;
} espnow_prov_resp_t;

static espnow_prov_init_t *g_prov_init = NULL;
static espnow_prov_resp_t *g_prov_resp = NULL;

static esp_err_t espnow_prov_responder_send(const espnow_addr_t *initiator_addr_list, size_t initiator_addr_num,
                                     const espnow_prov_wifi_t *wifi_config);

static esp_err_t espnow_prov_recv(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    uint8_t data_type = ((uint8_t *)data)[0];
    espnow_prov_data_t *recv_data = (espnow_prov_data_t *)data;

    switch (data_type) {
        case ESPNOW_PROV_TYPE_BEACON:
            ESP_LOGD(TAG, "ESPNOW_PROV_TYPE_BEACON");
            if (g_prov_init && g_prov_init->beacon_en) {
                g_prov_init->fix_ch = true;
                if (g_prov_init->scan_info.responder_info && g_prov_init->scan_info.rx_ctrl && g_prov_init->scan_info.addr) {
                    memcpy(g_prov_init->scan_info.responder_info, &recv_data->responder_info, sizeof(espnow_prov_responder_t));
                    memcpy(g_prov_init->scan_info.rx_ctrl, rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
                    memcpy(g_prov_init->scan_info.addr, src_addr, 6);
                }
            }
            break;

        case ESPNOW_PROV_TYPE_DEVICE:
            ESP_LOGD(TAG, "ESPNOW_PROV_TYPE_DEVICE");
            if (g_prov_resp && g_prov_resp->device_en) {
                ret = ESP_OK;
                if (g_prov_resp->device_cb) {
                    ret = g_prov_resp->device_cb(src_addr, &recv_data->initiator_info, size - 1, rx_ctrl);
                }
                if (ret == ESP_OK) {
                    espnow_addr_t initiator_addr = {0};
                    memcpy(initiator_addr, src_addr, 6);
                    ret = espnow_prov_responder_send((const espnow_addr_t *)&initiator_addr, 1, g_prov_resp->wifi_config);
                }
            }
            break;

        case ESPNOW_PROV_TYPE_WIFI:
            ESP_LOGD(TAG, "ESPNOW_PROV_TYPE_WIFI");
            if (g_prov_init && g_prov_init->wifi_en) {
                ret = ESP_OK;
                if (g_prov_init->wifi_cb) {
                    ret = g_prov_init->wifi_cb(src_addr, &recv_data->wifi_config, size - 1, rx_ctrl);
                }
                if (ret == ESP_OK) {
                    g_prov_init->config = true;
                }
            }
            break;

        default:
            break;
    }

    return ret;
}

esp_err_t espnow_prov_initiator_scan(espnow_addr_t responder_addr, espnow_prov_responder_t *responder_info,
                                    wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_addr);
    ESP_PARAM_CHECK(responder_info);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret                 = ESP_FAIL;

    wifi_country_t country = {0};
    uint32_t start_ticks = xTaskGetTickCount();

    if (!g_prov_init) {
        g_prov_init = ESP_CALLOC(1, sizeof(espnow_prov_init_t));
    }

    g_prov_init->beacon_en = true;
    g_prov_init->fix_ch = false;
    g_prov_init->scan_info.responder_info = responder_info;
    g_prov_init->scan_info.rx_ctrl = rx_ctrl;
    g_prov_init->scan_info.addr = responder_addr;

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 1, espnow_prov_recv);
    esp_wifi_get_country(&country);

    while (g_prov_init->fix_ch == false && (wait_ticks == portMAX_DELAY || xTaskGetTickCount() - start_ticks < wait_ticks)) {
        for (int i = 0; i < country.nchan && g_prov_init->fix_ch == false; ++i) {
            esp_wifi_set_channel(country.schan + i, WIFI_SECOND_CHAN_NONE);
            ESP_LOGD(TAG, "espnow_send, channel: %d", country.schan + i);
            vTaskDelay(pdMS_TO_TICKS(ESPNOW_PROV_BEACON_INTERVAL + 10));
        }
    }

    if (g_prov_init->fix_ch == true) {
        ret = ESP_OK;
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);
    ESP_FREE(g_prov_init);

    return ret;
}

esp_err_t espnow_prov_initiator_send(const espnow_addr_t responder_addr, const espnow_prov_initiator_t *initiator_info, espnow_prov_cb_t cb, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_addr);
    ESP_PARAM_CHECK(initiator_info);

    esp_err_t ret = ESP_ERR_WIFI_TIMEOUT;
    espnow_prov_data_t *prov_data = ESP_MALLOC(sizeof(espnow_prov_data_t));
    prov_data->type = ESPNOW_PROV_TYPE_DEVICE;
    memcpy(&prov_data->initiator_info, initiator_info, sizeof(espnow_prov_initiator_t));
    espnow_frame_head_t frame_head = {
        .filter_adjacent_channel = true,
        .security                = CONFIG_ESPNOW_PROV_SECURITY,
    };
    uint32_t start_ticks = xTaskGetTickCount();

    espnow_add_peer(responder_addr, NULL);

    ret = espnow_send(ESPNOW_DATA_TYPE_PROV, responder_addr, prov_data,
                      sizeof(espnow_prov_data_t), &frame_head, portMAX_DELAY);

    espnow_del_peer(responder_addr);
    ESP_FREE(prov_data);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    if (!g_prov_init) {
        g_prov_init = ESP_CALLOC(1, sizeof(espnow_prov_init_t));
    }
    g_prov_init->wifi_cb = cb;
    g_prov_init->wifi_en = true;
    g_prov_init->config = false;
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 1, espnow_prov_recv);

    while (g_prov_init->config == false && (wait_ticks == portMAX_DELAY || xTaskGetTickCount() - start_ticks < wait_ticks)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);

    if (g_prov_init->config == false) {
        ret = ESP_FAIL;
    } else {
        ret = ESP_OK;
    }

    ESP_FREE(g_prov_init);

    return ret;
}

static uint32_t g_beacon_stop_tick = 0;
static espnow_prov_data_t *g_beacon_prov_data = NULL;

static void responder_beacon_timercb(TimerHandle_t timer)
{
    if (g_beacon_stop_tick < xTaskGetTickCount()) {
        vTimerSetReloadMode(timer, pdFALSE);
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
        ESP_FREE(g_beacon_prov_data);
        ESP_LOGI(TAG, "Responder beacon end");
        g_prov_resp->device_cb = NULL;
        g_prov_resp->device_en = false;
        ESP_FREE(g_prov_resp->wifi_config);
        ESP_FREE(g_prov_resp);
        espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);
        return ;
    }

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
        .security                = CONFIG_ESPNOW_PROV_SECURITY,
    };

    espnow_send(ESPNOW_DATA_TYPE_PROV, ESPNOW_ADDR_BROADCAST, g_beacon_prov_data, 
                sizeof(espnow_prov_responder_t) + 1, &frame_head, portMAX_DELAY);
}

esp_err_t espnow_prov_responder_start(const espnow_prov_responder_t *responder_info, TickType_t wait_ticks, 
                                        const espnow_prov_wifi_t *wifi_config, espnow_prov_cb_t cb)
{
    ESP_PARAM_CHECK(responder_info);
    ESP_PARAM_CHECK(wifi_config);

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

    if (!g_prov_resp) {
        g_prov_resp = ESP_CALLOC(1, sizeof(espnow_prov_resp_t));
    }

    g_prov_resp->device_cb = cb;
    g_prov_resp->device_en = true;
    
    if (!g_prov_resp->wifi_config) {
        g_prov_resp->wifi_config = ESP_MALLOC(sizeof(espnow_prov_wifi_t));
    }
    memcpy(g_prov_resp->wifi_config, wifi_config, sizeof(espnow_prov_wifi_t));

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 1, espnow_prov_recv);

    return ESP_OK;
}

/**
 * @brief  Responder sends WiFi configuration
 *
 * @param[in]  initiator_addr_list  mac address list of initiators
 * @param[in]  initiator_addr_num  initiator address number
 * @param[in]  wifi_config  WiFi configuration information
 * 
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
static esp_err_t espnow_prov_responder_send(const espnow_addr_t *initiator_addr_list, size_t initiator_addr_num,
                                     const espnow_prov_wifi_t *wifi_config)
{
    ESP_PARAM_CHECK(initiator_addr_list);
    ESP_PARAM_CHECK(wifi_config);

    esp_err_t ret = ESP_OK;
    uint8_t dest_addr[6] = {0};
    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .security  = CONFIG_ESPNOW_PROV_SECURITY,
    };

    ESP_LOGD(TAG, MACSTR ", num: %d", MAC2STR(initiator_addr_list[0]), initiator_addr_num);

    if (initiator_addr_num > 1) {
        espnow_set_group(initiator_addr_list, initiator_addr_num, ESPNOW_ADDR_GROUP_PROV, NULL, true, portMAX_DELAY);
        frame_head.group = true;
        memcpy(dest_addr, ESPNOW_ADDR_GROUP_PROV, 6);
    } else {
        memcpy(dest_addr, initiator_addr_list[0], 6);
        espnow_add_peer(dest_addr, NULL);
    }

    size_t size = 1 + sizeof(espnow_prov_wifi_t) + wifi_config->custom_size;
    espnow_prov_data_t *prov_data = ESP_MALLOC(size);
    memcpy(&prov_data->wifi_config, wifi_config, size - 1);
    prov_data->type = ESPNOW_PROV_TYPE_WIFI;

    ret = espnow_send(ESPNOW_DATA_TYPE_PROV, dest_addr, prov_data, size, &frame_head, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_FREE(prov_data);
        ESP_LOGW(TAG, "[%s, %d] <%s> " "espnow_send", __func__, __LINE__, esp_err_to_name(ret));
        return ret;
    }

    ESP_FREE(prov_data);

    if (initiator_addr_num > 1) {
        espnow_set_group(initiator_addr_list, initiator_addr_num, ESPNOW_ADDR_GROUP_PROV, NULL, false, portMAX_DELAY);
    } else {
        espnow_del_peer(dest_addr);
    }

    return ESP_OK;
}
