/* Solution Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_utils.h"
#include "espnow.h"

#include "app_main.h"

#ifdef CONFIG_ESPNOW_CONTROL
#include "espnow_ctrl.h"
#include "driver/rmt.h"
#include "led_strip.h"
#include "button.h"
#endif

#include <wifi_provisioning/manager.h>

#ifdef CONFIG_WIFI_PROV
#include "wifi_prov.h"
#endif

#ifdef CONFIG_ESPNOW_INITIATOR
#include "initiator.h"
#endif

#ifdef CONFIG_ESPNOW_RESPONDER
#include "responder.h"
#endif

static const char *TAG = "app";

#ifdef CONFIG_IDF_TARGET_ESP32C3
#define CONFIG_LED_STRIP_GPIO GPIO_NUM_8
#else
#define CONFIG_LED_STRIP_GPIO GPIO_NUM_18
#endif

static led_strip_t *g_strip_handle = NULL;

#ifdef CONFIG_ESPNOW_CONTROL
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define BOOT_KEY_GPIIO        GPIO_NUM_9
#else
#define BOOT_KEY_GPIIO        GPIO_NUM_0
#endif

typedef enum {
    ESPNOW_CTRL_INIT,
    ESPNOW_CTRL_BOUND,
    ESPNOW_CTRL_MAX
} espnow_ctrl_status_t;

static espnow_ctrl_status_t s_espnow_ctrl_status = ESPNOW_CTRL_INIT;
#endif

#if defined(CONFIG_WIFI_PROV) || defined(CONFIG_ESPNOW_PROVISION)
#define CONFIG_WIFI_PROV_KEY        GPIO_NUM_2
typedef enum {
    ESPNOW_PROV_INIT,
    ESPNOW_PROV_START,
    ESPNOW_PROV_SUCCESS,
    ESPNOW_PROV_MAX
} espnow_prov_status_t;

static espnow_prov_status_t s_espnow_prov_status = ESPNOW_PROV_INIT;
#endif

static void light_init(void)
{
    g_strip_handle = led_strip_init(RMT_CHANNEL_0, CONFIG_LED_STRIP_GPIO, 1);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_strip_handle->set_pixel(g_strip_handle, 0, 255, 0, 0);
        g_strip_handle->refresh(g_strip_handle, 100);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
#if defined(CONFIG_WIFI_PROV) || defined(CONFIG_ESPNOW_PROVISION)
        s_espnow_prov_status = ESPNOW_PROV_SUCCESS;
#endif
        g_strip_handle->set_pixel(g_strip_handle, 0, 0, 255, 0);
        g_strip_handle->refresh(g_strip_handle, 100);
    }
}

#ifdef CONFIG_ESPNOW_CONTROL
static void initiator_send_press_cb(void *arg)
{
    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == button_get_event(arg)));
    ESP_LOGI(TAG, "initiator send press");
    static bool status = 0;
    if (s_espnow_ctrl_status == ESPNOW_CTRL_BOUND) {
        espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);
        status = !status;
    }
}

static void initiator_bind_press_cb(void *arg)
{
    ESP_ERROR_CHECK(!(BUTTON_DOUBLE_CLICK == button_get_event(arg)));
    ESP_LOGI(TAG, "initiator bind press");
    if (s_espnow_ctrl_status == ESPNOW_CTRL_INIT) {
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
        s_espnow_ctrl_status = ESPNOW_CTRL_BOUND;
    }
}

static void initiator_unbind_press_cb(void *arg)
{
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == button_get_event(arg)));
    ESP_LOGI(TAG, "initiator unbind press");
    if (s_espnow_ctrl_status == ESPNOW_CTRL_BOUND) {
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
        s_espnow_ctrl_status = ESPNOW_CTRL_INIT;
    }
}

static void control_button_init(void)
{
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = BOOT_KEY_GPIIO,
            .active_level = 0,
        },
    };

    button_handle_t button_handle = button_create(&button_config);
    button_register_cb(button_handle, BUTTON_SINGLE_CLICK, initiator_send_press_cb);
    button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, initiator_bind_press_cb);
    button_register_cb(button_handle, BUTTON_LONG_PRESS_START, initiator_unbind_press_cb);
}

static void responder_ctrl_data_cb(espnow_attribute_t initiator_attribute,
                                     espnow_attribute_t responder_attribute,
                                     uint32_t status)
{
    ESP_LOGI(TAG, "espnow_ctrl_responder_recv, initiator_attribute: %d, responder_attribute: %d, value: %d",
                initiator_attribute, responder_attribute, status);

    if (status) {
        g_strip_handle->set_pixel(g_strip_handle, 0, 255, 255, 255);
        g_strip_handle->refresh(g_strip_handle, 100);
    } else {
        g_strip_handle->clear(g_strip_handle, 100);
    }
}

static void espnow_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    if (base != ESP_EVENT_ESPNOW) {
        return;
    }

    switch (id) {
        case ESP_EVENT_ESPNOW_CTRL_BIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "bind, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            
            g_strip_handle->set_pixel(g_strip_handle, 0, 0x0, 255, 0x0);
            ESP_ERROR_CHECK(g_strip_handle->refresh(g_strip_handle, 100));
            break;
        }

        case ESP_EVENT_ESPNOW_CTRL_UNBIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "unbind, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            
            g_strip_handle->set_pixel(g_strip_handle, 0, 255, 0x0, 0x00);
            ESP_ERROR_CHECK(g_strip_handle->refresh(g_strip_handle, 100));
            break;
        }

        default:
        break;
    }
}

static void control_responder_init(void)
{
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_BIND, espnow_event_handler, NULL);
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_CTRL_UNBIND, espnow_event_handler, NULL);

    ESP_ERROR_CHECK(espnow_ctrl_responder_bind(30 * 1000, -55, NULL));
    espnow_ctrl_responder_data(responder_ctrl_data_cb);
}
#endif

#if defined(CONFIG_WIFI_PROV) || defined(CONFIG_ESPNOW_PROVISION)
static void single_press_cb(void *arg)
{
    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == button_get_event(arg)));
    ESP_LOGI(TAG, "ESPNOW provisioning press");

    if (s_espnow_prov_status == ESPNOW_PROV_SUCCESS) {
#ifdef CONFIG_ESPNOW_INITIATOR
        /*  Start 30s prov beacon */
        provision_beacon_start(30);
#endif
    }
}

static void wifi_prov_start_press_cb(void *arg)
{
    ESP_ERROR_CHECK(!(BUTTON_DOUBLE_CLICK == button_get_event(arg)));
    ESP_LOGI(TAG, "WiFi provisioning press");

    if (s_espnow_prov_status == ESPNOW_PROV_INIT) {
#ifdef CONFIG_WIFI_PROV
        wifi_prov();
#elif defined(CONFIG_ESPNOW_RESPONDER)
        provision_responder_start();
#endif
        s_espnow_prov_status = ESPNOW_PROV_START;

        g_strip_handle->set_pixel(g_strip_handle, 0, 255, 255, 255);
        g_strip_handle->refresh(g_strip_handle, 100);
    }
}

static void wifi_prov_reset_press_cb(void *arg)
{
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == button_get_event(arg)));
    ESP_LOGI(TAG, "Reset WiFi provisioning information and restart");

    wifi_prov_mgr_reset_provisioning();
    esp_wifi_disconnect();
    esp_restart();
}

static void prov_button_init(void)
{
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = CONFIG_WIFI_PROV_KEY,
            .active_level = 0,
        },
    };

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);

    button_handle_t button_handle = button_create(&button_config);
    button_register_cb(button_handle, BUTTON_SINGLE_CLICK, single_press_cb);
    button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, wifi_prov_start_press_cb);
    button_register_cb(button_handle, BUTTON_LONG_PRESS_START, wifi_prov_reset_press_cb);
}
#endif

static void hardware_init()
{
    light_init();

#if defined(CONFIG_WIFI_PROV) || defined(CONFIG_ESPNOW_PROVISION)
    prov_button_init();
#endif

#ifdef CONFIG_ESPNOW_INITIATOR
#ifdef CONFIG_ESPNOW_CONTROL
    control_button_init();
#endif
#endif

#ifdef CONFIG_ESPNOW_RESPONDER
#ifdef CONFIG_ESPNOW_CONTROL
    control_responder_init();
#endif
#endif
}

void app_main()
{
    init();

    hardware_init();

    start();
}
