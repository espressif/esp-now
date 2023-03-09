/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * Define the GPIOs which are used for the power lock and status LED on the coin cell button.
 */
#define BOARD_IO_PWRLOCK    GPIO_NUM_0
#define BOARD_IO_LED        GPIO_NUM_1
#define BOARD_IO_PIN_SEL_OUTPUT ((1ULL << BOARD_IO_PWRLOCK) | (1ULL << BOARD_IO_LED))

/**
 * @brief Lock the power.
 *
 * @note It still lock the power when en is false but hold the button.
 *
 * @param en true to lock, and false to unlock.
 */
void board_power_lock(bool en);

/**
 * @brief Set the status of the LED.
 *
 * @param en true to be ON, and false to be OFF.
 */
void board_led_on(bool en);

/**
 * @brief Initializing Hardware.
 *
 * @return
 *     - ESP_OK success
 *     - ESP_ERR_INVALID_ARG Parameter error
 */
esp_err_t board_init(void);

#ifdef __cplusplus
}
#endif
