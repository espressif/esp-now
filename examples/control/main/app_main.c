/* Control Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_utils.h"
#include "espnow.h"
#include "espnow_ctrl.h"

#include "driver/rmt.h"
#include "led_strip.h"
#include "button.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#define BOOT_KEY_GPIIO        GPIO_NUM_9
#define CONFIG_LED_STRIP_GPIO GPIO_NUM_8
#else
#define BOOT_KEY_GPIIO        GPIO_NUM_0
#define CONFIG_LED_STRIP_GPIO GPIO_NUM_18
#endif

static const char *TAG = "app_main";

typedef enum {
    ESPNOW_CTRL_INIT,
    ESPNOW_CTRL_BOUND,
    ESPNOW_CTRL_MAX
} espnow_ctrl_status_t;

static led_strip_t *g_strip_handle = NULL;
static espnow_ctrl_status_t s_espnow_ctrl_status = ESPNOW_CTRL_INIT;

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());

    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

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

static void app_driver_init(void)
{
    g_strip_handle = led_strip_init(RMT_CHANNEL_0, CONFIG_LED_STRIP_GPIO, 1);
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

static void responder_light_task(void *arg)
{
    espnow_attribute_t initiator_attribute;
    espnow_attribute_t responder_attribute;

    bool status   = true;

    app_driver_init();

    ESP_ERROR_CHECK(espnow_ctrl_responder_bind(30 * 1000, -55, NULL));
    while (espnow_ctrl_responder_recv(&initiator_attribute, &responder_attribute, (uint32_t *)&status) == ESP_OK) {
        ESP_LOGI(TAG, "espnow_ctrl_responder_recv, initiator_attribute: %d, responder_attribute: %d, value: %d",
                 initiator_attribute, responder_attribute, status);

        if (status) {
            g_strip_handle->set_pixel(g_strip_handle, 0, 255, 255, 255);
            g_strip_handle->refresh(g_strip_handle, 100);
        } else {
            g_strip_handle->clear(g_strip_handle, 100);
        }
    }

    ESP_LOGW(TAG, "Responder light task is exit");

    vTaskDelete(NULL);
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

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_storage_init();

    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, espnow_event_handler, NULL);

    xTaskCreate(responder_light_task, "responder_light_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
}
