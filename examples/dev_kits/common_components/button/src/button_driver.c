/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>

#include "esp_log.h"
#include "esp_adc_cal.h"

#include "button_driver.h"

#define BUTTON_BATTERY_ADC_CHANNEL         ADC1_CHANNEL_0
#define BUTTON_BATTERY_ADC_V_REF           (1100)
#define BUTTON_BATTERY_ADC_DIV             (10)

#define RTC_SLOW_MEM                       ((uint32_t*) (0x50000000))       /*!< RTC slow memory, 8k size */

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

/**
 * @brief Key status
 */
typedef struct {
    gpio_num_t gpio_num;
    button_key_status_t status;
    bool push;
    bool release;
    int backup_tickcount;
} button_key_t;

static const char *TAG            = "button_driver";
static xQueueHandle g_event_queue = NULL;
static button_key_t *g_button_key = NULL;
static button_config_t g_button_config = {0};

button_key_status_t button_key_get_status(uint8_t key_index)
{
    return g_button_key[key_index].status;
}

void button_key_reset_status()
{
    for (int i = 0; i < g_button_config.gpio_key_num; ++i) {
        g_button_key[i].status = BUTTON_KEY_NONE;
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t key_index = (uint32_t) arg;
    xQueueSendFromISR(g_event_queue, &key_index, NULL);
}

static void button_get_value_task(void *arg)
{
    uint32_t key_index = 0;

    for (;;) {
        if (!xQueueReceive(g_event_queue, &key_index, pdMS_TO_TICKS(100))) {
            for (int i = 0; i < g_button_config.gpio_key_num; ++i) {
                uint32_t spent_timer_ms = esp_timer_get_time() / 1000 - g_button_key[i].backup_tickcount;

                if (!g_button_key[i].push) {
                    continue;
                }

                if (spent_timer_ms >= g_button_config.time_llong_press && g_button_key[i].status != BUTTON_KEY_LLONG_PRESS_PUSH) {
                    g_button_key[i].status = BUTTON_KEY_LLONG_PRESS_PUSH;
                    esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_LLONG_PRESS_PUSH, (void *)&i, sizeof(int), 0);
                } else if (spent_timer_ms < g_button_config.time_llong_press
                           && spent_timer_ms >= g_button_config.time_long_press
                           && g_button_key[i].status != BUTTON_KEY_LONG_PRESS_PUSH) {
                    g_button_key[i].status  = BUTTON_KEY_LONG_PRESS_PUSH;
                    esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_LONG_PRESS_PUSH, (void *)&i, sizeof(int), 0);
                }
            }

            continue;
        }

        int key_level = gpio_get_level(g_button_key[key_index].gpio_num);
        vTaskDelay(pdMS_TO_TICKS(5));

        if (key_level != gpio_get_level(g_button_key[key_index].gpio_num)) {
            continue;
        }

        ESP_LOGD(TAG, "key_index: %d, gpio_num: %d, status: %d, key_level: %d",
                 key_index, g_button_key[key_index].gpio_num, g_button_key[key_index].status, key_level);

        if (key_level == 1 && !g_button_key[key_index].push && g_button_key[key_index].status != BUTTON_KEY_NONE) {
            g_button_key[key_index].push             = true;
            g_button_key[key_index].backup_tickcount = esp_timer_get_time() / 1000;
            esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_SHORT_PRESS_PUSH, (void *)&key_index, sizeof(int), 0);
            g_button_key[key_index].status = BUTTON_KEY_NONE;
        } else if (key_level == 0 && g_button_key[key_index].push) {
            g_button_key[key_index].release = true;
        }

        if ((g_button_key[key_index].push && g_button_key[key_index].release)) {
            uint32_t spent_timer_ms = esp_timer_get_time() / 1000 - g_button_key[key_index].backup_tickcount;
            g_button_key[key_index].push    = false;
            g_button_key[key_index].release = false;

            if (spent_timer_ms < g_button_config.time_long_press) {
                g_button_key[key_index].status = BUTTON_KEY_SHORT_PRESS_RELEASE;
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_SHORT_PRESS_RELEASE, (void *)&key_index, sizeof(int), 0);
                ESP_LOGD(TAG, "short_press, gpio_num: %d", g_button_key[key_index].gpio_num);
            } else if (spent_timer_ms < g_button_config.time_llong_press) {
                g_button_key[key_index].status = BUTTON_KEY_LONG_PRESS_RELEASE;
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_LONG_PRESS_RELEASE, (void *)&key_index, sizeof(int), 0);
                ESP_LOGD(TAG, "long_press, gpio_num: %d", g_button_key[key_index].gpio_num);
            } else {
                g_button_key[key_index].status = BUTTON_KEY_LLONG_PRESS_RELEASE;
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_LLONG_PRESS_RELEASE, (void *)&key_index, sizeof(int), 0);
                ESP_LOGD(TAG, "llong_press, gpio_num: %d", g_button_key[key_index].gpio_num);
            }
        }
    }

    vTaskDelete(NULL);
}

esp_err_t button_driver_init(button_config_t *config)
{
    memcpy(&g_button_config, config, sizeof(button_config_t));

    /**
     * @brief must set BUTTON_GPIO_POWER_SWITCH to high electrical level first, because
     *        the gpio_config may output one low electrical level pulse to the BUTTON_GPIO_POWER_SWITCH
     */
    gpio_config_t power_config = {
        .pin_bit_mask = BIT64(g_button_config.gpio_power),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    gpio_config(&power_config);
    gpio_set_level(g_button_config.gpio_power, true);

    /**
     * @brief key
     */
    g_button_key = calloc(g_button_config.gpio_key_num, sizeof(button_key_t));
    // memcpy(g_button_key, RTC_SLOW_MEM, sizeof(button_key_t));

    gpio_config_t key_config = {
        .intr_type    = GPIO_INTR_ANYEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };

    for (int i = 0; i < g_button_config.gpio_key_num; ++i) {
        g_button_key[i].gpio_num = g_button_config.gpio_key[i];
        key_config.pin_bit_mask |= BIT64(g_button_key[i].gpio_num);
    }

    gpio_config(&key_config);

    /**< Create a queue to handle gpio event from isr */
    g_event_queue = xQueueCreate(3, sizeof(uint32_t));

    /**< Install gpio isr service */
    ESP_ERROR_CHECK(gpio_install_isr_service(false));

    /**< Hook isr handler for specific gpio pin */
    for (uint32_t i = 0; i < g_button_config.gpio_key_num; ++i) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(g_button_key[i].gpio_num, gpio_isr_handler, (void *)i));
        ESP_LOGD(TAG, "gpio_num: %d, level: %d", g_button_key[i].gpio_num, gpio_get_level(g_button_key[i].gpio_num));

        if (gpio_get_level(g_button_key[i].gpio_num)) {
            g_button_key[i].push   = true;
            g_button_key[i].status = BUTTON_KEY_NONE;
            g_button_key[i].backup_tickcount = esp_timer_get_time() / 1000 - 100;
            esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_SHORT_PRESS_PUSH, (void *)&i, sizeof(int), portMAX_DELAY);
        } else if (g_button_key[i].push) {
            g_button_key[i].push    = false;
            g_button_key[i].release = false;
            g_button_key[i].status  = BUTTON_KEY_SHORT_PRESS_RELEASE;
            esp_event_post(BUTTON_EVENT, BUTTON_EVENT_KEY_LONG_PRESS_RELEASE, (void *)&i, sizeof(int), portMAX_DELAY);
        }
    }

    xTaskCreate(button_get_value_task, "button_get_value", 4096, NULL, 15, NULL);
    return ESP_OK;
}

esp_err_t button_driver_deinit()
{
    ESP_LOGI(TAG, "Power down");
    gpio_set_level(g_button_config.gpio_power, false);

    return ESP_OK;
}
