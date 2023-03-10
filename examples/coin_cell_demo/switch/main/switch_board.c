/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/gpio.h"
#include "switch_board.h"
#include "esp_log.h"
#include "esp_sleep.h"

static esp_err_t _board_gpio_init(void)
{
    esp_err_t ret;
    // zero-initialize the config structure.
    gpio_config_t io_conf = {};

    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pins that you want to set
    io_conf.pin_bit_mask = BOARD_IO_PIN_SEL_OUTPUT;
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;

    // configure GPIO with the given settings
    ret = gpio_config(&io_conf);

    gpio_set_level(BOARD_IO_PWRLOCK, 1);
    gpio_set_level(BOARD_IO_LED, 1);

    return ret;
}

void board_power_lock(bool en)
{
    if (en) {
        gpio_set_level(BOARD_IO_PWRLOCK, 1);
        gpio_hold_en(BOARD_IO_PWRLOCK);
    } else {
        gpio_hold_dis(BOARD_IO_PWRLOCK);
        gpio_set_level(BOARD_IO_PWRLOCK, 0);
    }
}

void board_led_on(bool en)
{
    if (en) {
        gpio_set_level(BOARD_IO_LED, 1);
        gpio_hold_en(BOARD_IO_LED);
    } else {
        gpio_hold_dis(BOARD_IO_LED);
        gpio_set_level(BOARD_IO_LED, 0);
    }
}

esp_err_t board_init(void)
{
    esp_err_t ret;
    ret = _board_gpio_init();
    return ret;
}
