// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

#include "esp_event.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* Event source task related definitions */
ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);

#define BUTTON_EVENT_CHARGING_STOPED            0x01
#define BUTTON_EVENT_CHARGING_COMPLETED         0x02
#define BUTTON_EVENT_KEY_SHORT_PRESS_PUSH       0x03
#define BUTTON_EVENT_KEY_SHORT_PRESS_RELEASE    0x04
#define BUTTON_EVENT_KEY_LONG_PRESS_PUSH        0x05
#define BUTTON_EVENT_KEY_LONG_PRESS_RELEASE     0x06
#define BUTTON_EVENT_KEY_LLONG_PRESS_PUSH       0x07
#define BUTTON_EVENT_KEY_LLONG_PRESS_RELEASE    0x08


/**
 * @brief The state when the button is triggered
 */
typedef enum {
    BUTTON_KEY_NONE                 = 0, /**< Button not triggered */
    BUTTON_KEY_SHORT_PRESS_RELEASE  = 1, /**< The button was short pressed */
    BUTTON_KEY_LONG_PRESS_PUSH      = 2, /**< The button was long pressed */
    BUTTON_KEY_LONG_PRESS_RELEASE   = 3, /**< Press and hold the button to lift */
    BUTTON_KEY_LLONG_PRESS_PUSH     = 4, /**< The button was long pressed */
    BUTTON_KEY_LLONG_PRESS_RELEASE  = 5, /**< Press and hold the button to lift */
} button_key_status_t;

typedef struct {
    uint8_t gpio_key_num;
    uint8_t gpio_key[10];
    uint8_t gpio_power;
    uint8_t gpio_battery;
    uint32_t time_long_press;
    uint32_t time_llong_press;
} button_config_t;

/**
 * @brief  Button initialize
 *
 * @return
 *      - ESP_OK
 *      - ESP_ERR_INVALID_ARG
 */
esp_err_t button_driver_init(button_config_t *config);

/**
 * @brief  Button deinitialize
 *
 * @return
 *      - ESP_OK
 *      - ESP_ERR_INVALID_ARG
 */
esp_err_t button_driver_deinit();

/**
 * @brief Whether to complete the battery charging
 *
 *
 * @return Button charging status
 */
bool button_battery_get_status();

/**
 * @brief The battery level of the button, this value is a percentage of the total battery
 * @return Battery power
 */
uint8_t button_battery_get_electricity();

/**
 *@brief Get the status record of the key
 *
 * @param  key_num Key sequence number
 * @return Key status
 */
button_key_status_t button_key_get_status(uint8_t key_num);

/**
 * @brief Reset key status record
 */
void button_key_reset_status();

#ifdef __cplusplus
}
#endif
