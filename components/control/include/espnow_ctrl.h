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

#pragma once

#include "esp_now.h"
#include "espnow.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#define ESP_EVENT_ESPNOW_CTRL_BIND      (ESP_EVENT_ESPNOW_CTRL_BASE + 0)
#define ESP_EVENT_ESPNOW_CTRL_UNBIND    (ESP_EVENT_ESPNOW_CTRL_BASE + 1)

/**
 * @brief The first byte code device type, the second byte code device attribute
 */
typedef enum {
    ESPNOW_ATTRIBUTE_BASE           = 0x0000,
    ESPNOW_ATTRIBUTE_POWER          = 0x0001,
    ESPNOW_ATTRIBUTE_POWER_ADD      = 0x0002,

    ESPNOW_ATTRIBUTE_ATTRIBUTE      = 0x0003,

    /**< light */
    ESPNOW_ATTRIBUTE_LIGHT_BASE     = 0x0100,
    ESPNOW_ATTRIBUTE_BRIGHTNESS     = 0x0101,
    ESPNOW_ATTRIBUTE_BRIGHTNESS_ADD = 0x0102,
    ESPNOW_ATTRIBUTE_HUE            = 0x0103,
    ESPNOW_ATTRIBUTE_HUE_ADD        = 0x0104,
    ESPNOW_ATTRIBUTE_SATURATION     = 0x0105,
    ESPNOW_ATTRIBUTE_SATURATION_ADD = 0x0106,
    ESPNOW_ATTRIBUTE_WARM           = 0x0107,
    ESPNOW_ATTRIBUTE_WARM_ADD       = 0x0108,
    ESPNOW_ATTRIBUTE_CLOD           = 0x0109,
    ESPNOW_ATTRIBUTE_CLOD_ADD       = 0x010a,
    ESPNOW_ATTRIBUTE_RED            = 0x010b,
    ESPNOW_ATTRIBUTE_RED_ADD        = 0x010c,
    ESPNOW_ATTRIBUTE_GREEN          = 0x010d,
    ESPNOW_ATTRIBUTE_GREEN_ADD      = 0x010e,
    ESPNOW_ATTRIBUTE_BLUE           = 0x010f,
    ESPNOW_ATTRIBUTE_BLUE_ADD       = 0x0110,
    ESPNOW_ATTRIBUTE_MODE           = 0x0111,
    ESPNOW_ATTRIBUTE_MODE_ADD       = 0x0112,

    /**< button */
    ESPNOW_BUTTON_ATTRIBUTE         = 0x0200,
    ESPNOW_ATTRIBUTE_KEY_1          = 0x0201,
    ESPNOW_ATTRIBUTE_KEY_2          = 0x0202,
    ESPNOW_ATTRIBUTE_KEY_3          = 0x0203,
    ESPNOW_ATTRIBUTE_KEY_4          = 0x0204,
    ESPNOW_ATTRIBUTE_KEY_5          = 0x0205,
    ESPNOW_ATTRIBUTE_KEY_6          = 0x0206,
    ESPNOW_ATTRIBUTE_KEY_7          = 0x0207,
    ESPNOW_ATTRIBUTE_KEY_8          = 0x0208,
    ESPNOW_ATTRIBUTE_KEY_9          = 0x0209,
    ESPNOW_ATTRIBUTE_KEY_10         = 0x0210,
} espnow_attribute_t;

typedef struct {
    uint8_t mac[6];
    uint16_t initiator_attribute;
} espnow_ctrl_bind_info_t;

typedef struct {
    uint16_t initiator_attribute;
    uint16_t responder_attribute;
    union {
        bool responder_value_b;   /** Boolean */
        int responder_value_i;    /** Integer */
        float responder_value_f;  /** Float */
        struct {
            uint32_t responder_value_s_flag : 24; // 0x00, 0x00, 0x00
            uint8_t responder_value_s_size; 
        };
    };

    char responder_value_s[0];   /** NULL terminated string */
} espnow_ctrl_data_t;

typedef bool (* espnow_ctrl_bind_cb_t)(espnow_attribute_t initiator_attribute, uint8_t mac[6], uint8_t rssi);

esp_err_t espnow_ctrl_initiator_bind(espnow_attribute_t initiator_attribute, bool enable);
esp_err_t espnow_ctrl_initiator_send(espnow_attribute_t initiator_attribute, espnow_attribute_t responder_attribute, uint32_t responder_value);

esp_err_t espnow_ctrl_responder_bind(uint32_t wait_ms, int8_t rssi, espnow_ctrl_bind_cb_t cb);
esp_err_t espnow_ctrl_responder_recv(espnow_attribute_t *initiator_attribute, espnow_attribute_t *responder_attribute, uint32_t *responder_value);


esp_err_t espnow_ctrl_responder_get_bindlist(espnow_ctrl_bind_info_t *list, size_t *size);
esp_err_t espnow_ctrl_responder_set_bindlist(const espnow_ctrl_bind_info_t *info);
esp_err_t espnow_ctrl_responder_remove_bindlist(const espnow_ctrl_bind_info_t *info);

/**
 * 
 */
esp_err_t espnow_ctrl_send(const espnow_addr_t dest_addr, const espnow_ctrl_data_t *data, const espnow_frame_head_t *frame_head, TickType_t wait_ticks);
esp_err_t espnow_ctrl_recv(espnow_addr_t src_addr, espnow_ctrl_data_t *data, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);

#ifdef __cplusplus
}
#endif /**< _cplusplus */