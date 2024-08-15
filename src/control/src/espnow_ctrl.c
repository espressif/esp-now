/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_now.h"
#include "esp_log.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "esp_event_base.h"

#include "espnow.h"
#include "espnow_ctrl.h"
#include "espnow_mem.h"
#include "espnow_storage.h"
#include "espnow_utils.h"


extern wifi_country_t g_self_country;
typedef struct {
    size_t size;
    bool   is_init;
    espnow_ctrl_bind_info_t data[ESPNOW_BIND_LIST_MAX_SIZE];
} espnow_bindlist_t;

static const char *TAG = "espnow_ctrl";

static espnow_bindlist_t g_bindlist = {0};

int8_t g_rssi = 0;
uint32_t g_timestamp = 0;
espnow_ctrl_bind_cb_t g_bind_cb = NULL;
espnow_ctrl_data_cb_t g_data_cb = NULL;
espnow_ctrl_data_raw_cb_t g_data_raw_cb = NULL;

#ifdef CONFIG_ESPNOW_ALL_SECURITY
#define CONFIG_ESPNOW_CONTROL_SECURITY 1
#else
#ifndef CONFIG_ESPNOW_CONTROL_SECURITY
#define CONFIG_ESPNOW_CONTROL_SECURITY 0
#endif
#endif

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
#define ESPNOW_CHANNEL_KEY       "ch_key"
#define RESEND_SCAN_COUNT_MAX (sizeof(scan_channel_sequence) * 2)

#ifndef CONFIG_ESPNOW_VERSION
#define ESPNOW_VERSION                  2
#else
#define ESPNOW_VERSION                  CONFIG_ESPNOW_VERSION
#endif

static SemaphoreHandle_t g_bind_sem = NULL;

typedef struct {
    uint8_t type    : 4;
    uint8_t version : 2;
    uint8_t         : 2;
    uint8_t size;
    espnow_frame_head_t frame_head;
    uint8_t dest_addr[6];
    uint8_t src_addr[6];
    uint8_t payload[0];
} __attribute__((packed)) espnow_forward_data_t;
#else
static const espnow_frame_head_t g_initiator_frame = {
    .retransmit_count = 10,
    .broadcast        = true,
    .channel          = ESPNOW_CHANNEL_ALL,
    .forward_ttl      = 10,
    .forward_rssi     = -25,
    .security         = CONFIG_ESPNOW_CONTROL_SECURITY,
};
#endif

static void espnow_load_bindlist(void)
{
    if(!g_bindlist.is_init) {
        espnow_storage_get("bindlist", &g_bindlist, sizeof(g_bindlist));
        g_bindlist.is_init = true;
    }
}

static bool espnow_ctrl_responder_is_bindlist(const uint8_t *mac, espnow_attribute_t initiator_attribute)
{
    espnow_load_bindlist();
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
    espnow_load_bindlist();
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
    espnow_load_bindlist();
    if (!espnow_ctrl_responder_is_bindlist(info->mac, info->initiator_attribute)) {
        memcpy(g_bindlist.data + g_bindlist.size, info, sizeof(espnow_ctrl_bind_info_t));
        g_bindlist.size++;
        espnow_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
    }

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_remove_bindlist(const espnow_ctrl_bind_info_t *info)
{
    espnow_load_bindlist();
    for (int i = 0; i < g_bindlist.size; ++i) {
        if (!memcmp(g_bindlist.data + i, info, sizeof(espnow_ctrl_bind_info_t))) {
            g_bindlist.size--;

            if (g_bindlist.size > 0) {
                g_bindlist.data[g_bindlist.size].initiator_attribute = info->initiator_attribute;
                memcpy(g_bindlist.data[g_bindlist.size].mac, info->mac, 6);
            }

            espnow_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
            break;
        }
    }

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_clear_bindlist(void)
{
    memset(g_bindlist.data, 0, sizeof(espnow_ctrl_bind_info_t)*ESPNOW_BIND_LIST_MAX_SIZE);
    g_bindlist.size = 0;
    espnow_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));

    return ESP_OK;
}

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_FORWARD
static esp_err_t espnow_ctrl_responder_forward(uint8_t type, uint8_t *src_addr, const void *data, size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    uint8_t primary           = 0;
    wifi_second_chan_t second = 0;
    int retransmit_count = 0;
    espnow_ctrl_data_t *ctrl_data = (espnow_ctrl_data_t *)data;
    espnow_forward_data_t *espnow_data = ESP_MALLOC(sizeof(espnow_forward_data_t) + size);

    espnow_data->type = type;
    espnow_data->version = ESPNOW_VERSION;
    espnow_data->size = size;
    memcpy(&espnow_data->frame_head, &ctrl_data->frame_head, sizeof(espnow_frame_head_t));
    memcpy(espnow_data->dest_addr, ESPNOW_ADDR_BROADCAST, 6);
    memcpy(espnow_data->src_addr, src_addr, 6);
    memcpy(espnow_data->payload, data, size);

    esp_wifi_get_channel(&primary, &second);
    while (retransmit_count < g_self_country.nchan) {
        esp_wifi_set_channel(g_self_country.schan + retransmit_count, WIFI_SECOND_CHAN_NONE);
        esp_now_send(ESPNOW_ADDR_BROADCAST, (uint8_t *)espnow_data, espnow_data->size + sizeof(espnow_forward_data_t));
        retransmit_count++;
    }
    esp_wifi_set_channel(primary, second);

    return ESP_OK;
}
#endif

static esp_err_t espnow_ctrl_responder_bind_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_ctrl_data_t *ctrl_data = (espnow_ctrl_data_t *)data;

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
        if (ctrl_data->frame_head.ack) {
            espnow_send(ESPNOW_DATA_TYPE_ACK, ESPNOW_ADDR_BROADCAST, &ctrl_data->frame_head, sizeof(ctrl_data->frame_head), &ctrl_data->frame_head, pdMS_TO_TICKS(100));
        }
#endif

    if (ctrl_data->responder_value_b) {
        ESP_LOGI(TAG, "bind, timestamp: %d, max timestamp: %d, rssi: %d, min rssi: %d, bindlist size: %d",
                    esp_log_timestamp(), g_timestamp, rx_ctrl->rssi, g_rssi, g_bindlist.size);

        bool bind_cb_flag = false;

        if (g_bind_cb) {
            bind_cb_flag = g_bind_cb(ctrl_data->initiator_attribute, src_addr, rx_ctrl->rssi);
        } else {
            bind_cb_flag = true;
        }

        espnow_ctrl_bind_error_t bind_error = ESPNOW_BIND_ERROR_NONE;
        if (esp_log_timestamp() > g_timestamp) {
            bind_error = ESPNOW_BIND_ERROR_TIMEOUT;
        } else if (rx_ctrl->rssi < g_rssi) {
            bind_error = ESPNOW_BIND_ERROR_RSSI;
        } else if (g_bindlist.size >= ESPNOW_BIND_LIST_MAX_SIZE) {
            bind_error = ESPNOW_BIND_ERROR_LIST_FULL;
        }

        if (bind_error != ESPNOW_BIND_ERROR_NONE) {
            esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_BIND_ERROR,
                &bind_error, sizeof(bind_error), 0);
        } else if (bind_cb_flag){
            if (!espnow_ctrl_responder_is_bindlist(src_addr, ctrl_data->initiator_attribute)) {
                g_bindlist.data[g_bindlist.size].initiator_attribute = ctrl_data->initiator_attribute;
                memcpy(g_bindlist.data[g_bindlist.size].mac, src_addr, 6);

                esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_BIND,
                                g_bindlist.data + g_bindlist.size, sizeof(espnow_ctrl_bind_info_t), 0);
#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
                vTaskDelay(pdMS_TO_TICKS(100));
#endif
                g_bindlist.size++;
                espnow_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
            }
        }
    } else {
        for (int i = 0; i < g_bindlist.size; ++i) {
            if (!memcmp(g_bindlist.data[i].mac, src_addr, 6)
                    && g_bindlist.data[i].initiator_attribute == ctrl_data->initiator_attribute) {
                esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_UNBIND,
                                g_bindlist.data + i, sizeof(espnow_ctrl_bind_info_t), 0);
#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
                vTaskDelay(pdMS_TO_TICKS(100));
#endif

                g_bindlist.size--;

                for (int j = i; j < g_bindlist.size; j++) {
                    g_bindlist.data[j].initiator_attribute = g_bindlist.data[j + 1].initiator_attribute;
                    memcpy(g_bindlist.data[j].mac, g_bindlist.data[j + 1].mac, 6);
                }
                g_bindlist.data[g_bindlist.size].initiator_attribute = 0;
                memset(g_bindlist.data[g_bindlist.size].mac, 0, 6);

                espnow_storage_set("bindlist", &g_bindlist, sizeof(g_bindlist));
                break;
            }
        }
    }

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_FORWARD
    espnow_ctrl_responder_forward(ESPNOW_DATA_TYPE_CONTROL_BIND, src_addr, data, size, rx_ctrl);
#endif

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_bind(uint32_t wait_ms, int8_t rssi, espnow_ctrl_bind_cb_t cb)
{
    espnow_load_bindlist();

    g_bind_cb   = cb;
    g_timestamp = esp_log_timestamp() + wait_ms;
    g_rssi      = rssi;

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_CONTROL_BIND, true, espnow_ctrl_responder_bind_process);

    return ESP_OK;
}

static esp_err_t espnow_ctrl_responder_data_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_ctrl_data_t *ctrl_data = (espnow_ctrl_data_t *)data;
    ESP_LOGD(TAG, "src_addr: "MACSTR", espnow_ctrl_responder_recv, value: %d",
                MAC2STR(src_addr), ctrl_data->responder_value_i);

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
    if (ctrl_data->frame_head.ack) {
        espnow_send(ESPNOW_DATA_TYPE_ACK, ESPNOW_ADDR_BROADCAST, &ctrl_data->frame_head, sizeof(ctrl_data->frame_head), &ctrl_data->frame_head, pdMS_TO_TICKS(100));
    }
#endif

    if (espnow_ctrl_responder_is_bindlist(src_addr, ctrl_data->initiator_attribute)) {
        if (g_data_cb) {
            g_data_cb(ctrl_data->initiator_attribute, ctrl_data->responder_attribute, ctrl_data->responder_value_i);
        }

        if (g_data_raw_cb) {
            g_data_raw_cb(src_addr, ctrl_data, rx_ctrl);
        }

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_FORWARD
        espnow_ctrl_responder_forward(ESPNOW_DATA_TYPE_CONTROL_DATA, src_addr, data, size, rx_ctrl);
#endif
    }

    return ESP_OK;
}

esp_err_t espnow_ctrl_responder_data(espnow_ctrl_data_cb_t cb)
{
    g_data_cb        = cb;
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_CONTROL_DATA, 1, espnow_ctrl_responder_data_process);

    return ESP_OK;
}

#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
static esp_err_t espnow_ctrl_initiator_ack(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    espnow_frame_head_t *frame_head = (espnow_frame_head_t *)data;

    uint8_t channel = frame_head ? frame_head->channel : 1;

    ESP_LOGI(TAG, "src_addr: "MACSTR", %s, channel: %d", MAC2STR(src_addr), __func__, channel);

    espnow_storage_set(ESPNOW_CHANNEL_KEY, &channel, sizeof(channel));

    if (g_bind_sem) {
        xSemaphoreGive(g_bind_sem);
    }

    return ESP_OK;
}

static esp_err_t espnow_ctrl_initiator_handle(espnow_data_type_t type, espnow_attribute_t initiator_attribute, espnow_attribute_t responder_attribute, uint32_t responder_value)
{
    esp_err_t ret = ESP_OK;

    g_bind_sem = xSemaphoreCreateBinary();
    if (!g_bind_sem) {
        return ESP_FAIL;
    }

    BaseType_t bind_sem_ret = pdFAIL;
    int retransmit_count = 0;
    uint8_t channel = 1;
#ifdef CONFIG_ESPNOW_LIGHT_SLEEP
    bool timer_wakeup_enabled = false;
#endif
    espnow_storage_get(ESPNOW_CHANNEL_KEY, &channel, sizeof(channel));
    espnow_ctrl_data_t data = {
        .frame_head = {
            .broadcast                  = true,
            .forward_ttl                = CONFIG_ESPNOW_CONTROL_FORWARD_TTL,
            .forward_rssi               = CONFIG_ESPNOW_CONTROL_FORWARD_RSSI,
            .magic                      = esp_random(),
            .ack                        = true,
            .channel                    = channel,
            .filter_adjacent_channel    = true,
            .security                   = CONFIG_ESPNOW_CONTROL_SECURITY,
        },
        .initiator_attribute = initiator_attribute,
        .responder_attribute = responder_attribute,
        .responder_value_i   = responder_value
    };
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_ACK, true, espnow_ctrl_initiator_ack);

    do {
        espnow_send(type, ESPNOW_ADDR_BROADCAST, &data, sizeof(espnow_ctrl_data_t), &data.frame_head, portMAX_DELAY);
        bind_sem_ret = xSemaphoreTake(g_bind_sem, pdMS_TO_TICKS(CONFIG_ESPNOW_CONTROL_WAIT_ACK_DURATION));

        if (bind_sem_ret == pdPASS) {
            break;
        }
#ifdef CONFIG_ESPNOW_LIGHT_SLEEP
        esp_wifi_force_wakeup_release();
        esp_sleep_enable_timer_wakeup(CONFIG_ESPNOW_LIGHT_SLEEP_DURATION * 1000);
        timer_wakeup_enabled = true;
        esp_light_sleep_start();
        esp_wifi_force_wakeup_acquire();
#endif
    } while (retransmit_count ++ < CONFIG_ESPNOW_CONTROL_RETRANSMISSION_TIMES);

    if (bind_sem_ret != pdPASS) {
        for (int i = 0; i < g_self_country.nchan; i++) {
            if (g_self_country.schan + i == channel) {
                continue;
            }
            data.frame_head.channel = g_self_country.schan + i;
            retransmit_count = 0;
            do {
                espnow_send(type, ESPNOW_ADDR_BROADCAST, &data, sizeof(espnow_ctrl_data_t), &data.frame_head, portMAX_DELAY);
                bind_sem_ret = xSemaphoreTake(g_bind_sem, pdMS_TO_TICKS(CONFIG_ESPNOW_CONTROL_WAIT_ACK_DURATION));
                if (bind_sem_ret == pdPASS) {
                    ret = ESP_OK;
                    break;
                } else {
                    ret = ESP_FAIL;
                }
#ifdef CONFIG_ESPNOW_LIGHT_SLEEP
                if (retransmit_count < CONFIG_ESPNOW_CONTROL_RETRANSMISSION_TIMES || (i < g_self_country.nchan - 1 &&
                    // !(Second last channel, however last channel is the saved channel)
                    !(i == g_self_country.nchan - 2 && g_self_country.schan + i + 1 == channel))) {
                    esp_wifi_force_wakeup_release();
                    esp_sleep_enable_timer_wakeup(CONFIG_ESPNOW_LIGHT_SLEEP_DURATION * 1000);
                    timer_wakeup_enabled = true;
                    esp_light_sleep_start();
                    esp_wifi_force_wakeup_acquire();
                }
#endif
            } while (retransmit_count ++ < CONFIG_ESPNOW_CONTROL_RETRANSMISSION_TIMES);
            if (ret == ESP_OK) {
                break;
            }
        }
    }
#ifdef CONFIG_ESPNOW_LIGHT_SLEEP
    if (timer_wakeup_enabled) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
#endif
    vSemaphoreDelete(g_bind_sem);
    g_bind_sem = NULL;

    ESP_ERROR_RETURN(ret != ESP_OK, ret,  "espnow_broadcast, ret: %d", ret);

    return ESP_OK;
}

esp_err_t espnow_ctrl_initiator_bind(espnow_attribute_t initiator_attribute, bool enable)
{
    return espnow_ctrl_initiator_handle(ESPNOW_DATA_TYPE_CONTROL_BIND, initiator_attribute, ESPNOW_ATTRIBUTE_BASE, enable);
}

esp_err_t espnow_ctrl_initiator_send(espnow_attribute_t initiator_attribute,
                                     espnow_attribute_t responder_attribute,
                                     uint32_t responder_value)
{
    return espnow_ctrl_initiator_handle(ESPNOW_DATA_TYPE_CONTROL_DATA, initiator_attribute, responder_attribute, responder_value);
}
#else
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

    ret = espnow_send(ESPNOW_DATA_TYPE_CONTROL_BIND, ESPNOW_ADDR_BROADCAST, &data,
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

    ret = espnow_send(ESPNOW_DATA_TYPE_CONTROL_DATA, ESPNOW_ADDR_BROADCAST, &data, 
                      sizeof(espnow_ctrl_data_t), &g_initiator_frame, pdMS_TO_TICKS(1000));
    ESP_ERROR_RETURN(ret != ESP_OK, ret,  "espnow_broadcast, ret: %d", ret);

    return ESP_OK;
}
#endif

esp_err_t espnow_ctrl_send(const espnow_addr_t dest_addr, const espnow_ctrl_data_t *data, const espnow_frame_head_t *frame_head, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(dest_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(frame_head);

    uint8_t size = (!data->responder_value_s_flag && data->responder_value_s_size) ? data->responder_value_s_size : 0;

    esp_err_t ret = espnow_send(ESPNOW_DATA_TYPE_CONTROL_DATA, ESPNOW_ADDR_BROADCAST, data, 
                      sizeof(espnow_ctrl_data_t) + size, frame_head, wait_ticks);
    ESP_ERROR_RETURN(ret != ESP_OK, ret,  "espnow_broadcast, ret: %d", ret);

    return ESP_OK;
}

esp_err_t espnow_ctrl_recv(espnow_ctrl_data_raw_cb_t cb)
{
    g_data_raw_cb        = cb;
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_CONTROL_DATA, 1, espnow_ctrl_responder_data_process);

    return ESP_OK;
}
