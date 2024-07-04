/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow_utils.h"

#include "espnow.h"
#include "espnow_ctrl.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/rmt.h"
#endif
#include "led_strip.h"

#include "driver/gpio.h"

// All the default GPIOs are based on ESP32 series DevKitC boards, for other boards, please modify them accordingly.
#ifdef CONFIG_IDF_TARGET_ESP32C2
#define CONTROL_KEY_GPIO      GPIO_NUM_9
#define LED_RED_GPIO          GPIO_NUM_0
#define LED_GREEN_GPIO        GPIO_NUM_1
#define LED_BLUE_GPIO         GPIO_NUM_8
#elif CONFIG_IDF_TARGET_ESP32C3
#define CONTROL_KEY_GPIO      GPIO_NUM_9
#define LED_STRIP_GPIO        GPIO_NUM_8
#elif CONFIG_IDF_TARGET_ESP32
#define CONTROL_KEY_GPIO      GPIO_NUM_0
// There is not LED module in ESP32 DevKitC board, so you need to connect one by yourself.
#define LED_STRIP_GPIO        GPIO_NUM_18
#elif CONFIG_IDF_TARGET_ESP32S2
#define CONTROL_KEY_GPIO      GPIO_NUM_0
#define LED_STRIP_GPIO        GPIO_NUM_18
#elif CONFIG_IDF_TARGET_ESP32S3
#define CONTROL_KEY_GPIO      GPIO_NUM_0
// For old version board, the number is 48.
#define LED_STRIP_GPIO        GPIO_NUM_38
#elif CONFIG_IDF_TARGET_ESP32C6
#define CONTROL_KEY_GPIO      GPIO_NUM_9
#define LED_STRIP_GPIO        GPIO_NUM_8
#endif

#define UNBIND_TOTAL_COUNT               (5)

#define BULB_STATUS_KEY                  "bulb_key"

static const char *TAG = "app_bulb";

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
// ESP32C2-DevKit Board uses RGB LED
#if !CONFIG_IDF_TARGET_ESP32C2
static led_strip_handle_t g_strip_handle = NULL;
#endif
#else
static led_strip_t *g_strip_handle = NULL;
#endif

static uint32_t s_bulb_status = 0;

static char *bind_error_to_string(espnow_ctrl_bind_error_t bind_error)
{
    switch (bind_error) {
    case ESPNOW_BIND_ERROR_NONE: {
        return "No error";
        break;
    }

    case ESPNOW_BIND_ERROR_TIMEOUT: {
        return "bind timeout";
        break;
    }

    case ESPNOW_BIND_ERROR_RSSI: {
        return "bind packet RSSI below expected threshold";
        break;
    }

    case ESPNOW_BIND_ERROR_LIST_FULL: {
        return "bindlist is full";
        break;
    }

    default: {
        return "unknown error";
        break;
    }
    }
}

static void app_set_bulb_status(void)
{
    espnow_storage_set(BULB_STATUS_KEY, &s_bulb_status, sizeof(s_bulb_status));
}

static void app_get_bulb_status(void)
{
    espnow_storage_get(BULB_STATUS_KEY, &s_bulb_status, sizeof(s_bulb_status));
}

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

static void app_led_init(void)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#if CONFIG_IDF_TARGET_ESP32C2
    gpio_reset_pin(LED_RED_GPIO);
    gpio_reset_pin(LED_GREEN_GPIO);
    gpio_reset_pin(LED_BLUE_GPIO);

    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LED_RED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_BLUE_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(LED_RED_GPIO, 1);
    gpio_set_level(LED_GREEN_GPIO, 1);
    gpio_set_level(LED_BLUE_GPIO, 1);
#else
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &g_strip_handle));
    /* Set all LED off to clear all pixels */
    led_strip_clear(g_strip_handle);
#endif
#else
    g_strip_handle = led_strip_init(RMT_CHANNEL_0, LED_STRIP_GPIO, 1);
#endif
}

void app_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#if CONFIG_IDF_TARGET_ESP32C2
    gpio_set_level(LED_RED_GPIO, red > 0 ? 0 : 1);
    gpio_set_level(LED_GREEN_GPIO, green > 0 ? 0 : 1);
    gpio_set_level(LED_BLUE_GPIO, blue > 0 ? 0 : 1);
#else
    led_strip_set_pixel(g_strip_handle, 0, red, green, blue);
    led_strip_refresh(g_strip_handle);
#endif
#else
    g_strip_handle->set_pixel(g_strip_handle, 0, red, green, blue);
    g_strip_handle->refresh(g_strip_handle, 100);
#endif
}

static void app_driver_init(void)
{
    app_led_init();
}

static void app_bulb_ctrl_data_cb(espnow_attribute_t initiator_attribute,
                              espnow_attribute_t responder_attribute,
                              uint32_t status)
{
    ESP_LOGI(TAG, "app_bulb_ctrl_data_cb, initiator_attribute: %d, responder_attribute: %d, value: %" PRIu32 "",
             initiator_attribute, responder_attribute, status);
    /* status = 0: OFF, 1: ON, 2: TOGGLE */
    if (status != s_bulb_status) {
        s_bulb_status ^= 1;
        app_set_bulb_status();
        if (s_bulb_status) {
            app_led_set_color(255, 255, 255);
        } else {
            app_led_set_color(0, 0, 0);
        }
    }
}

static void app_bulb_init(void)
{
    ESP_ERROR_CHECK(espnow_ctrl_responder_bind(30 * 60 * 1000, -55, NULL));
    espnow_ctrl_responder_data(app_bulb_ctrl_data_cb);
}

static void app_espnow_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base != ESP_EVENT_ESPNOW) {
        return;
    }

    switch (id) {
    case ESP_EVENT_ESPNOW_CTRL_BIND: {
        espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
        ESP_LOGI(TAG, "bind, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);

        app_led_set_color(0, 255, 0);
        break;
    }

    case ESP_EVENT_ESPNOW_CTRL_BIND_ERROR: {
        espnow_ctrl_bind_error_t *bind_error = (espnow_ctrl_bind_error_t *)event_data;
        ESP_LOGW(TAG, "bind error: %s", bind_error_to_string(*bind_error));
        break;
    }

    case ESP_EVENT_ESPNOW_CTRL_UNBIND: {
        espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
        ESP_LOGI(TAG, "unbind, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);

        app_led_set_color(255, 0, 0);
        break;
    }

    default:
        break;
    }
}

static esp_err_t unbind_restore_init(void)
{
    if (espnow_reboot_unbroken_count() >= UNBIND_TOTAL_COUNT) {
        ESP_LOGI(TAG, "unbind restore");
        espnow_storage_erase("bindlist");
        app_led_set_color(255, 0, 0);
        return espnow_reboot(CONFIG_ESPNOW_REBOOT_UNBROKEN_INTERVAL_TIMEOUT);
    }

    return ESP_OK;
}

void app_main(void)
{
    espnow_storage_init();
    app_driver_init();

    unbind_restore_init();

    app_get_bulb_status();
    if (s_bulb_status) {
        app_led_set_color(255, 255, 255);
    } else {
        app_led_set_color(0, 0, 0);
    }

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, app_espnow_event_handler, NULL);

    app_bulb_init();
}