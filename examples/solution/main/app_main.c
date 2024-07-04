/* Solution Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow.h"
#include "espnow_utils.h"

#ifdef CONFIG_APP_ESPNOW_CONTROL
#include "espnow_ctrl.h"
#endif

#include "iot_button.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/rmt.h"
#endif
#include "led_strip.h"

#include <wifi_provisioning/manager.h>

#ifdef CONFIG_APP_WIFI_PROVISION
#include "wifi_prov.h"
#endif

#ifdef CONFIG_APP_ESPNOW_INITIATOR
#include "initiator.h"
#endif

#ifdef CONFIG_APP_ESPNOW_RESPONDER
#include "responder.h"
#endif

static const char *TAG = "app";

// All the default GPIOs are based on ESP32 series DevKitC boards, for other boards, please modify them accordingly.
#ifdef CONFIG_IDF_TARGET_ESP32C2
#define LED_RED_GPIO          GPIO_NUM_0
#define LED_GREEN_GPIO        GPIO_NUM_1
#define LED_BLUE_GPIO         GPIO_NUM_8
#elif CONFIG_IDF_TARGET_ESP32C3
#define LED_STRIP_GPIO        GPIO_NUM_8
#elif CONFIG_IDF_TARGET_ESP32
// There is not LED module in ESP32 DevKitC board, so you need to connect one by yourself.
#define LED_STRIP_GPIO        GPIO_NUM_18
#elif CONFIG_IDF_TARGET_ESP32S2
#define LED_STRIP_GPIO        GPIO_NUM_18
#elif CONFIG_IDF_TARGET_ESP32S3
// For old version board, the number is 48.
#define LED_STRIP_GPIO        GPIO_NUM_38
#elif CONFIG_IDF_TARGET_ESP32C6
#define LED_STRIP_GPIO        GPIO_NUM_8
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#if !CONFIG_IDF_TARGET_ESP32C2
static led_strip_handle_t g_strip_handle = NULL;
#endif
#else
static led_strip_t *g_strip_handle = NULL;
#endif

#if defined(CONFIG_APP_ESPNOW_CONTROL) && defined(CONFIG_APP_ESPNOW_INITIATOR)
#if CONFIG_IDF_TARGET_ESP32C2 || CONFIG_IDF_TARGET_ESP32C3
#define CONTROL_KEY_GPIO        GPIO_NUM_9
#else
#define CONTROL_KEY_GPIO        GPIO_NUM_0
#endif

typedef enum {
    APP_ESPNOW_CTRL_INIT,
    APP_ESPNOW_CTRL_BOUND,
    APP_ESPNOW_CTRL_MAX
} app_espnow_ctrl_status_t;

static app_espnow_ctrl_status_t s_espnow_ctrl_status = APP_ESPNOW_CTRL_INIT;
#endif

#if defined(CONFIG_APP_WIFI_PROVISION) || defined(CONFIG_APP_ESPNOW_PROVISION)

#define WIFI_PROV_KEY_GPIO      GPIO_NUM_2

typedef enum {
    APP_WIFI_PROV_INIT,
    APP_WIFI_PROV_START,
    APP_WIFI_PROV_SUCCESS,
    APP_WIFI_PROV_MAX
} app_wifi_prov_status_t;

static app_wifi_prov_status_t s_wifi_prov_status = APP_WIFI_PROV_INIT;
#endif

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

static void app_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_led_set_color(255, 0, 0);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
#if defined(CONFIG_APP_WIFI_PROVISION) || defined(CONFIG_APP_ESPNOW_PROVISION)
        s_wifi_prov_status = APP_WIFI_PROV_SUCCESS;
#endif

        app_led_set_color(0, 255, 0);
    }
}

#ifdef CONFIG_APP_ESPNOW_CONTROL
#if CONFIG_APP_ESPNOW_INITIATOR
static void app_initiator_send_press_cb(void *arg, void *usr_data)
{
    static bool status = 0;

    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == iot_button_get_event(arg)));

    if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_BOUND) {
        ESP_LOGI(TAG, "initiator send press");
        espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);
        status = !status;
    } else {
        ESP_LOGI(TAG, "please double click to bind the devices firstly");
    }
}

static void app_initiator_bind_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_DOUBLE_CLICK == iot_button_get_event(arg)));

    if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_INIT) {
        ESP_LOGI(TAG, "initiator bind press");
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
        s_espnow_ctrl_status = APP_ESPNOW_CTRL_BOUND;
    } else {
        ESP_LOGI(TAG, "this device is already in bound status");
    }
}

static void app_initiator_unbind_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == iot_button_get_event(arg)));

    if (s_espnow_ctrl_status == APP_ESPNOW_CTRL_BOUND) {
        ESP_LOGI(TAG, "initiator unbind press");
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
        s_espnow_ctrl_status = APP_ESPNOW_CTRL_INIT;
    } else {
        ESP_LOGI(TAG, "this device is not been bound");
    }
}

static void app_control_button_init(void)
{
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = CONTROL_KEY_GPIO,
            .active_level = 0,
        },
    };

    button_handle_t button_handle = iot_button_create(&button_config);

    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, app_initiator_send_press_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, app_initiator_bind_press_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, app_initiator_unbind_press_cb, NULL);
}

#elif CONFIG_APP_ESPNOW_RESPONDER

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

static void app_responder_ctrl_data_cb(espnow_attribute_t initiator_attribute,
                                       espnow_attribute_t responder_attribute,
                                       uint32_t status)
{
    ESP_LOGI(TAG, "app_responder_ctrl_data_cb, initiator_attribute: %d, responder_attribute: %d, value: %" PRIu32 "",
             initiator_attribute, responder_attribute, status);

    if (status) {
        app_led_set_color(255, 255, 255);
    } else {
        app_led_set_color(0, 0, 0);
    }
}

static void app_control_responder_init(void)
{
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_BIND, app_espnow_event_handler, NULL);
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_UNBIND, app_espnow_event_handler, NULL);

    ESP_ERROR_CHECK(espnow_ctrl_responder_bind(30 * 1000, -55, NULL));
    espnow_ctrl_responder_data(app_responder_ctrl_data_cb);
}
#endif
#endif

#if defined(CONFIG_APP_WIFI_PROVISION) || defined(CONFIG_APP_ESPNOW_PROVISION)
#ifdef CONFIG_APP_ESPNOW_INITIATOR
static void app_wifi_prov_over_espnow_start_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == iot_button_get_event(arg)));

    if (s_wifi_prov_status == APP_WIFI_PROV_SUCCESS) {
        bool enabled;

        espnow_get_config_for_data_type(ESPNOW_DATA_TYPE_PROV, &enabled);

        if (enabled) {
            ESP_LOGI(TAG, "WiFi provisioning over ESP-NOW is started");
        } else {
            ESP_LOGI(TAG, "Start WiFi provisioning over ESP-NOW on initiator");

            /*  Start 30s prov beacon */
            app_espnow_prov_beacon_start(30);
        }
    } else if (s_wifi_prov_status == APP_WIFI_PROV_START) {
        ESP_LOGI(TAG, "Please finish WiFi provisioning firstly");
    } else {
        ESP_LOGI(TAG, "Please start WiFi provisioning firstly");
    }
}
#endif

static void app_wifi_prov_start_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_DOUBLE_CLICK == iot_button_get_event(arg)));

    if (s_wifi_prov_status == APP_WIFI_PROV_INIT) {
#ifdef CONFIG_APP_WIFI_PROVISION
        ESP_LOGI(TAG, "Starting WiFi provisioning on initiator");

        wifi_prov();
#elif defined(CONFIG_APP_ESPNOW_RESPONDER)
        ESP_LOGI(TAG, "Start WiFi provisioning over ESP-NOW on responder");

        app_espnow_prov_responder_start();
#endif
        s_wifi_prov_status = APP_WIFI_PROV_START;

        app_led_set_color(255, 255, 255);
    } else if (s_wifi_prov_status == APP_WIFI_PROV_START) {
        ESP_LOGI(TAG, "WiFi provisioning is started");
    } else {
        ESP_LOGI(TAG, "WiFi is already provisioned");
    }
}

static void app_wifi_prov_reset_press_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == iot_button_get_event(arg)));

    ESP_LOGI(TAG, "Reset WiFi provisioning information and restart");

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
    wifi_prov_mgr_reset_provisioning();
#endif

    esp_wifi_disconnect();
    esp_restart();
}

static void app_wifi_prov_button_init(void)
{
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = WIFI_PROV_KEY_GPIO,
            .active_level = 0,
        },
    };

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, app_wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, app_wifi_event_handler, NULL);

    button_handle_t button_handle = iot_button_create(&button_config);

#ifdef CONFIG_APP_ESPNOW_INITIATOR
    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, app_wifi_prov_over_espnow_start_press_cb, NULL);
#endif
    iot_button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, app_wifi_prov_start_press_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, app_wifi_prov_reset_press_cb, NULL);
}
#endif

static void app_wifi_init()
{
#if CONFIG_APP_WIFI_PROVISION
    wifi_prov_init();
#else
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
#endif
}

void app_main()
{
    espnow_storage_init();

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();

    espnow_config.qsize = CONFIG_APP_ESPNOW_QUEUE_SIZE;
#ifdef CONFIG_APP_ESPNOW_SECURITY
    espnow_config.sec_enable = 1;
#endif

    espnow_init(&espnow_config);

    app_led_init();

#if defined(CONFIG_APP_WIFI_PROVISION) || defined(CONFIG_APP_ESPNOW_PROVISION)
    app_wifi_prov_button_init();
#endif

#if CONFIG_APP_ESPNOW_INITIATOR
    app_espnow_initiator_register();

#ifdef CONFIG_APP_ESPNOW_CONTROL
    app_control_button_init();
#endif

    app_espnow_initiator();
#elif CONFIG_APP_ESPNOW_RESPONDER
    app_espnow_responder_register();

#ifdef CONFIG_APP_ESPNOW_CONTROL
    app_control_responder_init();
#endif

    app_espnow_responder();
#endif
}
