/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_sleep.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow_utils.h"

#include "espnow.h"
#include "espnow_ctrl.h"

#include "iot_button.h"

#include "switch_board.h"

#define BULB_STATUS_KEY       "bulb_key"

static const char *TAG = "app_switch";

static void app_wifi_init()
{
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#ifdef CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON
#define SEND_GAP_TIME         30
#define LONG_PRESS_SLEEP_TIME 2000

typedef enum {
    ESPNOW_TASK_STATE_SEND_RECORD,
    ESPNOW_TASK_STATE_SEND_SCAN,
    ESPNOW_TASK_STATE_SEND_BIND,
    ESPNOW_TASK_STATE_BIND_DONE,
    ESPNOW_TASK_STATE_SEND_UNBIND,
    ESPNOW_TASK_STATE_UNBIND_DONE,
    ESPNOW_TASK_STATE_DONE,
} espnow_task_state_t;

static espnow_task_state_t task_state = ESPNOW_TASK_STATE_SEND_RECORD;

static void set_light_sleep(uint32_t ms)
{
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_light_sleep_start();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *cause_str;
    switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
        cause_str = "GPIO";
        break;
    case ESP_SLEEP_WAKEUP_UART:
        cause_str = "UART";
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        cause_str = "timer";
        break;
    default:
        cause_str = "unknown";
        break;
    }
    ESP_LOGI(TAG, "Woke up from: %s", cause_str);
}

static void control_task(void *pvParameter)
{
    board_led_on(true);
    board_power_lock(true);
    set_light_sleep(SEND_GAP_TIME);

    for (;;) {
        if (task_state == ESPNOW_TASK_STATE_SEND_RECORD) {
            bool status = 0;
            espnow_storage_get(BULB_STATUS_KEY, &status, sizeof(status));
            status = !status;
            espnow_storage_set(BULB_STATUS_KEY, &status, sizeof(status));
            ESP_LOGI(TAG, "switch send press");
            espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);

            task_state = ESPNOW_TASK_STATE_DONE;
        }

        if (task_state == ESPNOW_TASK_STATE_DONE) {
            set_light_sleep(SEND_GAP_TIME); //Prevent false wake-ups
            board_power_lock(false);
            board_led_on(false);
            ESP_LOGW(TAG, "task_state = ESPNOW_TASK_STATE_DONE");
            set_light_sleep(LONG_PRESS_SLEEP_TIME);
            board_power_lock(true);
            board_led_on(true);
            task_state = ESPNOW_TASK_STATE_SEND_BIND;
        }

        if (task_state == ESPNOW_TASK_STATE_SEND_BIND) {
            ESP_LOGI(TAG, "switch bind press");
            espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
            task_state = ESPNOW_TASK_STATE_BIND_DONE;
        }

        if (task_state == ESPNOW_TASK_STATE_BIND_DONE) {
            set_light_sleep(SEND_GAP_TIME); //Prevent false wake-ups
            board_power_lock(false);
            board_led_on(false);
            ESP_LOGW(TAG, "task_state = ESPNOW_TASK_STATE_DONE");
            set_light_sleep(LONG_PRESS_SLEEP_TIME);
            board_power_lock(true);
            board_led_on(true);
            task_state = ESPNOW_TASK_STATE_SEND_UNBIND;
        }

        if (task_state == ESPNOW_TASK_STATE_SEND_UNBIND) {
            ESP_LOGI(TAG, "switch unbind press");
            espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
            task_state = ESPNOW_TASK_STATE_UNBIND_DONE;
        }

        if (task_state == ESPNOW_TASK_STATE_UNBIND_DONE) {
            set_light_sleep(SEND_GAP_TIME); //Prevent false wake-ups
            board_power_lock(false);
            board_led_on(false);
            set_light_sleep(LONG_PRESS_SLEEP_TIME);
        }
    }
}
#else
// All the default GPIOs are based on ESP32 series DevKitC boards, for other boards, please modify them accordingly.
#ifdef CONFIG_IDF_TARGET_ESP32C2
#define CONTROL_KEY_GPIO      GPIO_NUM_9
#elif CONFIG_IDF_TARGET_ESP32C3
#define CONTROL_KEY_GPIO      GPIO_NUM_9
#elif CONFIG_IDF_TARGET_ESP32
#define CONTROL_KEY_GPIO      GPIO_NUM_0
#elif CONFIG_IDF_TARGET_ESP32S2
#define CONTROL_KEY_GPIO      GPIO_NUM_0
#elif CONFIG_IDF_TARGET_ESP32S3
#define CONTROL_KEY_GPIO      GPIO_NUM_0
#elif CONFIG_IDF_TARGET_ESP32C6
#define CONTROL_KEY_GPIO      GPIO_NUM_9
#endif

static void app_switch_send_press_cb(void *arg, void *usr_data)
{
    bool status = 0;

    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == iot_button_get_event(arg)));

    ESP_LOGI(TAG, "switch send press");
    espnow_storage_get(BULB_STATUS_KEY, &status, sizeof(status));
    espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);
    status = !status;
    espnow_storage_set(BULB_STATUS_KEY, &status, sizeof(status));
}

static void app_switch_bind_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_DOUBLE_CLICK == iot_button_get_event(arg)));

    ESP_LOGI(TAG, "switch bind press");
    espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
}

static void app_switch_unbind_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == iot_button_get_event(arg)));

    ESP_LOGI(TAG, "switch unbind press");
    espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
}
#endif

static void app_driver_init(void)
{
#ifdef CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON
    board_init();
#else
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = CONTROL_KEY_GPIO,
            .active_level = 0,
        },
    };

    button_handle_t button_handle = iot_button_create(&button_config);

    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, app_switch_send_press_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, app_switch_bind_press_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, app_switch_unbind_press_cb, NULL);
#endif
}

void app_main(void)
{
    espnow_storage_init();

    app_wifi_init();
    app_driver_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.send_max_timeout = portMAX_DELAY;
    espnow_init(&espnow_config);

#ifdef CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON
    xTaskCreate(control_task, "control_task", 4096*2, NULL, 15, NULL);
#endif
}