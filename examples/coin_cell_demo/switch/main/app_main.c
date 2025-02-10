/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#if CONFIG_PM_ENABLE
#include "driver/gpio.h"
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow_utils.h"
#include "espnow.h"
#include "espnow_ctrl.h"
#if !defined(CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON) || !CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON_V1
#include "iot_button.h"
#endif
#ifdef CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON
#include "switch_board.h"
#endif

#if CONFIG_EXAMPLE_SWITCH_STATUS_PERSISTED
#define BULB_STATUS_KEY       "bulb_key"
#endif

static const char *TAG = "app_switch";

#if !defined(CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON) || !CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON_V1
static QueueHandle_t g_button_queue = NULL;

#if CONFIG_PM_ENABLE
void power_save_set(bool enable_light_sleep)
{
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 1, 0)
#if CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t pm_config = {
#elif CONFIG_IDF_TARGET_ESP32S2
    esp_pm_config_esp32s2_t pm_config = {
#elif CONFIG_IDF_TARGET_ESP32C3
    esp_pm_config_esp32c3_t pm_config = {
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config = {
#elif CONFIG_IDF_TARGET_ESP32C2
    esp_pm_config_esp32c2_t pm_config = {
#endif
#else // ESP_IDF_VERSION
    esp_pm_config_t pm_config = {
#endif
            .max_freq_mhz = CONFIG_EXAMPLE_MAX_CPU_FREQ_MHZ,
            .min_freq_mhz = CONFIG_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = enable_light_sleep
#endif
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );
}
#endif // CONFIG_PM_ENABLE

#endif

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

#if CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON_V1
static void set_light_sleep(uint32_t ms)
{
    esp_wifi_force_wakeup_release();
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_light_sleep_start();
    esp_wifi_force_wakeup_acquire();
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

static void control_task(void *pvParameter)
{
    board_led_on(true);
    board_power_lock(true);
    set_light_sleep(SEND_GAP_TIME);

    for (;;) {
        if (task_state == ESPNOW_TASK_STATE_SEND_RECORD) {
            uint8_t status = 0;
            /* status = 0: OFF, 1: ON, 2: TOGGLE */
#if CONFIG_EXAMPLE_SWITCH_STATUS_PERSISTED
            espnow_storage_get(BULB_STATUS_KEY, &status, sizeof(status));
            status ^= 1;
            espnow_storage_set(BULB_STATUS_KEY, &status, sizeof(status));
#else
            status = 2;
#endif
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
            ESP_LOGW(TAG, "task_state = ESPNOW_TASK_STATE_BIND_DONE");
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
#else // !V1

static void control_task(void *pvParameter)
{
    button_event_t evt_data;
    uint8_t status = 0;

    board_led_on(true);

    g_button_queue = xQueueCreate(5, sizeof(button_event_t));
    if (!g_button_queue) {
        ESP_LOGE(TAG, "Error creating queue.");
        return;
    }

    if (xQueueReceive(g_button_queue, &evt_data, portMAX_DELAY) != pdTRUE) {
        ESP_LOGI(TAG, "Nothing received");
        return;
    }
    iot_button_stop();
#if CONFIG_PM_ENABLE
    power_save_set(false);
#endif
#if CONFIG_PM_ENABLE || CONFIG_ESPNOW_LIGHT_SLEEP
    esp_wifi_force_wakeup_acquire();
#endif

    ESP_LOGI(TAG, "Button event: %d", evt_data);
    if (evt_data == BUTTON_SINGLE_CLICK) {
        ESP_LOGI(TAG, "switch send press");
#if CONFIG_EXAMPLE_SWITCH_STATUS_PERSISTED
        espnow_storage_get(BULB_STATUS_KEY, &status, sizeof(status));
        status ^= 1;
        espnow_storage_set(BULB_STATUS_KEY, &status, sizeof(status));
#else
        status = 2;
#endif
        /* status = 0: OFF, 1: ON, 2: TOGGLE */
        ESP_LOGI(TAG, "key status: %d", status);
        espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);
    } else if (evt_data == BUTTON_LONG_PRESS_UP) {
        ESP_LOGI(TAG, "switch bind press");
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
    } else if (evt_data == BUTTON_LONG_PRESS_START) {
        ESP_LOGI(TAG, "switch unbind press");
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
    } else {
        ESP_LOGI(TAG, "event not handled");
    }
#if CONFIG_PM_ENABLE || CONFIG_ESPNOW_LIGHT_SLEEP
    esp_wifi_force_wakeup_release();
#endif

    board_power_lock(false);
    board_led_on(false);

    esp_wifi_force_wakeup_release();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_light_sleep_start();
}

static void app_switch_button_event_cb(void *arg, void *usr_data)
{
    static bool light_status = false;
    button_event_t btn_evt = iot_button_get_event(arg);
    if (btn_evt == BUTTON_PRESS_DOWN) {
        // Make sure iot_button state machine has started.
        board_power_lock(true);
    } else if (btn_evt == BUTTON_LONG_PRESS_HOLD) {
        board_led_on(light_status);
        light_status = !light_status;
    } else {
        xQueueSend(g_button_queue, &btn_evt, pdMS_TO_TICKS(300));
    }
}

#endif
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

static void control_task(void *pvParameter)
{
    button_event_t evt_data;
    uint8_t status = 0;
    g_button_queue = xQueueCreate(5, sizeof(button_event_t));
    if (!g_button_queue) {
        ESP_LOGE(TAG, "Error creating queue.");
        return;
    }

#if CONFIG_PM_ENABLE
    gpio_wakeup_enable(CONTROL_KEY_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
#endif

    while(1) {
        if (xQueueReceive(g_button_queue, &evt_data, portMAX_DELAY) != pdTRUE) {
            ESP_LOGI(TAG, "Nothing received");
            continue;
        }
#if CONFIG_PM_ENABLE
        power_save_set(false);
#endif
#if CONFIG_PM_ENABLE || CONFIG_ESPNOW_LIGHT_SLEEP
        esp_wifi_force_wakeup_acquire();
#endif

        ESP_LOGI(TAG, "Button event: %d", evt_data);
        if (evt_data == BUTTON_SINGLE_CLICK) {
            ESP_LOGI(TAG, "switch send press");
#if CONFIG_EXAMPLE_SWITCH_STATUS_PERSISTED
            espnow_storage_get(BULB_STATUS_KEY, &status, sizeof(status));
            status ^= 1;
            espnow_storage_set(BULB_STATUS_KEY, &status, sizeof(status));
#else
            status = 2;
#endif
            /* status = 0: OFF, 1: ON, 2: TOGGLE */
            ESP_LOGI(TAG, "key status: %d", status);
            espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);
        } else if (evt_data == BUTTON_DOUBLE_CLICK) {
            ESP_LOGI(TAG, "switch bind press");
            espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
        } else if (evt_data == BUTTON_LONG_PRESS_START) {
            ESP_LOGI(TAG, "switch unbind press");
            espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
        } else {
            ESP_LOGI(TAG, "event not handled");
        }
#if CONFIG_PM_ENABLE || CONFIG_ESPNOW_LIGHT_SLEEP
        esp_wifi_force_wakeup_release();
#endif
#if CONFIG_PM_ENABLE
        power_save_set(true);
#endif
    }
    if (g_button_queue) {
        vQueueDelete(g_button_queue);
        g_button_queue = NULL;
    }
    vTaskDelete(NULL);
}

static void app_switch_button_event_cb(void *arg, void *usr_data)
{
    button_event_t btn_evt = iot_button_get_event(arg);
    xQueueSend(g_button_queue, &btn_evt, pdMS_TO_TICKS(300));
}
#endif

static void app_driver_init(void)
{
#ifdef CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON
    board_init();

#if !CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON_V1
    button_config_t button_config = {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config = {
            .adc_channel = BOARD_CHAN_ADC,
            .min = 2200,
            .max = 3400,
            .adc_handle = NULL,
        },
    };
    button_handle_t button_handle = iot_button_create(&button_config);
    iot_button_register_cb(button_handle, BUTTON_PRESS_DOWN, app_switch_button_event_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, app_switch_button_event_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_HOLD, app_switch_button_event_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_PRESS_END, app_switch_button_event_cb, NULL);

    button_event_config_t btn_event_cfg = {
        .event = BUTTON_LONG_PRESS_UP,
        .event_data = {
            .long_press.press_time = 2000,
        }
    };
    iot_button_register_event_cb(button_handle, btn_event_cfg, app_switch_button_event_cb, NULL);

    btn_event_cfg.event = BUTTON_LONG_PRESS_START;
    btn_event_cfg.event_data.long_press.press_time = 4000;
    iot_button_register_event_cb(button_handle, btn_event_cfg, app_switch_button_event_cb, NULL);
#endif

#else
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = CONTROL_KEY_GPIO,
            .active_level = 0,
#if CONFIG_GPIO_BUTTON_SUPPORT_POWER_SAVE
        .enable_power_save = true,
#endif
        },
    };

    button_handle_t button_handle = iot_button_create(&button_config);

    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, app_switch_button_event_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, app_switch_button_event_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, app_switch_button_event_cb, NULL);
#endif
}

void app_main(void)
{
    espnow_storage_init();

    app_wifi_init();
    app_driver_init();

#if !defined(CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON) || !CONFIG_EXAMPLE_USE_COIN_CELL_BUTTON_V1
#if CONFIG_PM_ENABLE
    power_save_set(true);
#endif // CONFIG_PM_ENABLE
#endif

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.send_max_timeout = portMAX_DELAY;
    espnow_init(&espnow_config);
#if CONFIG_ESPNOW_LIGHT_SLEEP
    esp_now_set_wake_window(0);
#endif
    xTaskCreate(control_task, "control_task", 4096*2, NULL, 15, NULL);
}
