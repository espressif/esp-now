/* Remote Control Button Example

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
#include "esp_storage.h"

#include "led_pwm.h"
#include "button_driver.h"
#include "espnow_ctrl.h"

#define EVENT_GROUP_DEVICE_KEY_PRESS          BIT0
#define EVENT_GROUP_DEVICE_KEY_RELEASE        BIT1
#define EVENT_GROUP_DEVICE_KEY_LONG_PRESS     BIT2
#define EVENT_GROUP_DEVICE_KEY_LONG_RELEASE   BIT3
#define EVENT_GROUP_DEVICE_KEY_LLONG_PRESS    BIT4
#define EVENT_GROUP_DEVICE_KEY_LLONG_RELEASE  BIT5

enum light_channel {
    CHANNEL_ID_RED   = 0,
    CHANNEL_ID_GREEN = 1,
    CHANNEL_ID_BLUE  = 2,
};

static const char *TAG = "main";
static EventGroupHandle_t g_event_group_trigger = NULL;

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void espnow_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base == ESP_EVENT_ESPNOW) {
        // espnow_info_t *info = (espnow_info_t *)event_data;
        // ESP_LOGI(TAG, "band, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->uuid), info->initiator_type);
    } else if (base == BUTTON_EVENT) {
        ESP_LOGI(TAG, "Button event, id: %d, key_num: %d", id, *((uint32_t *)event_data));

        switch (id) {
            case BUTTON_EVENT_KEY_SHORT_PRESS_RELEASE:
                xEventGroupSetBits(g_event_group_trigger, EVENT_GROUP_DEVICE_KEY_RELEASE);
                break;

            case BUTTON_EVENT_KEY_SHORT_PRESS_PUSH:
                xEventGroupSetBits(g_event_group_trigger, EVENT_GROUP_DEVICE_KEY_PRESS);
                break;

            case BUTTON_EVENT_KEY_LONG_PRESS_PUSH:
                xEventGroupSetBits(g_event_group_trigger, EVENT_GROUP_DEVICE_KEY_LONG_PRESS);
                break;

            case BUTTON_EVENT_KEY_LONG_PRESS_RELEASE:
                xEventGroupSetBits(g_event_group_trigger, EVENT_GROUP_DEVICE_KEY_LONG_RELEASE);
                break;

            case BUTTON_EVENT_KEY_LLONG_PRESS_RELEASE:
                xEventGroupSetBits(g_event_group_trigger, EVENT_GROUP_DEVICE_KEY_LONG_RELEASE);
                break;

            case BUTTON_EVENT_KEY_LLONG_PRESS_PUSH:
                xEventGroupSetBits(g_event_group_trigger, EVENT_GROUP_DEVICE_KEY_LLONG_PRESS);
                break;

            default:
                break;
        }
    }
}

void app_main(void)
{
    g_event_group_trigger = xEventGroupCreate();
    button_config_t config = {
        .gpio_key_num     = 4,
        .gpio_key         = {GPIO_NUM_32, GPIO_NUM_39, GPIO_NUM_35, GPIO_NUM_34},
        .gpio_power       = GPIO_NUM_5,
        .time_long_press  = 2000,
        .time_llong_press = 5000,
    };

    esp_log_level_set("*", ESP_LOG_INFO);

    esp_event_loop_create_default();
    esp_event_handler_register(BUTTON_EVENT, ESP_EVENT_ANY_ID, espnow_event_handler, NULL);

    button_driver_init(&config);

    esp_reboot(pdMS_TO_TICKS(15 * 1000));

    led_pwm_init(LEDC_TIMER_0, LEDC_LOW_SPEED_MODE, 1000);
    led_pwm_regist_channel(CHANNEL_ID_RED, GPIO_NUM_27);
    led_pwm_regist_channel(CHANNEL_ID_GREEN, GPIO_NUM_26);
    led_pwm_regist_channel(CHANNEL_ID_BLUE, GPIO_NUM_14);

    esp_storage_init();

    uint32_t status[4] = {0};
    esp_storage_get("key_status", status, sizeof(status));

    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

    for (int wait_ms = 100;; wait_ms = 10000) {
        EventBits_t uxBits = xEventGroupWaitBits(g_event_group_trigger,
                             EVENT_GROUP_DEVICE_KEY_PRESS | EVENT_GROUP_DEVICE_KEY_LONG_PRESS
                             | EVENT_GROUP_DEVICE_KEY_RELEASE | EVENT_GROUP_DEVICE_KEY_LONG_RELEASE
                             | EVENT_GROUP_DEVICE_KEY_LLONG_PRESS,
                             pdTRUE, pdFALSE, pdMS_TO_TICKS(wait_ms));

        if (!uxBits) {
            break;
        }

        if (uxBits & EVENT_GROUP_DEVICE_KEY_PRESS) {
            led_pwm_set_channel(CHANNEL_ID_GREEN, 255, 0);
        }

        if (uxBits & EVENT_GROUP_DEVICE_KEY_LONG_PRESS) {
            ESP_LOGI(TAG, "Bind device");
            led_pwm_start_blink(CHANNEL_ID_RED, 128, 200, 0);
            led_pwm_start_blink(CHANNEL_ID_GREEN, 128, 200, 0);
            led_pwm_set_channel(CHANNEL_ID_BLUE, 0, 0);

            espnow_ctrl_initiator_bind(ESPNOW_BUTTON_ATTRIBUTE, true);
        } else if (uxBits & EVENT_GROUP_DEVICE_KEY_LLONG_PRESS) {
            ESP_LOGI(TAG, "Unbind device");
            led_pwm_start_blink(CHANNEL_ID_RED, 255, 200, 0);
            led_pwm_set_channel(CHANNEL_ID_GREEN, 0, 0);
            led_pwm_set_channel(CHANNEL_ID_BLUE, 0, 0);

            espnow_ctrl_initiator_bind(ESPNOW_BUTTON_ATTRIBUTE, false);
        } else if (uxBits & EVENT_GROUP_DEVICE_KEY_RELEASE) {
            uint32_t responder_value               = 0;
            espnow_attribute_t responder_attribute = 0;

            if (button_key_get_status(0)) {
                responder_attribute = ESPNOW_ATTRIBUTE_POWER;
                status[0] = !status[0];
                responder_value     = status[0];
            } else if (button_key_get_status(1)) {
                status[1]       = status[1] > 100 ? 100 : status[1] + 20;
                responder_value = status[1];
                responder_attribute = ESPNOW_ATTRIBUTE_BRIGHTNESS;
            } else if (button_key_get_status(2)) {
                status[2]           = status[2] >= 300 ? 0 : status[2] + 60;
                responder_value     = status[2];
                responder_attribute = ESPNOW_ATTRIBUTE_HUE;
            } else if (button_key_get_status(3)) {
                status[1]           = status[1] <= 0 ? 0 : status[1] - 20;
                responder_value     = status[1];
                responder_attribute = ESPNOW_ATTRIBUTE_BRIGHTNESS;
            }

            button_key_reset_status();

            uint32_t timestamp_start = esp_log_timestamp();
            espnow_ctrl_initiator_send(ESPNOW_BUTTON_ATTRIBUTE, responder_attribute, responder_value);
            ESP_LOGI(TAG, "data, <%u> responder_attribute: %d, responder_value_i: %d",
                     esp_log_timestamp() - timestamp_start, responder_attribute, responder_value);

            esp_storage_set("key_status", status, sizeof(status));
        }

        if (uxBits & EVENT_GROUP_DEVICE_KEY_RELEASE || uxBits & EVENT_GROUP_DEVICE_KEY_LONG_RELEASE
                || uxBits & EVENT_GROUP_DEVICE_KEY_LLONG_RELEASE) {
            break;
        }
    }

    led_pwm_set_channel(CHANNEL_ID_RED, 0, 0);
    led_pwm_set_channel(CHANNEL_ID_GREEN, 0, 0);
    led_pwm_set_channel(CHANNEL_ID_BLUE, 0, 0);
    button_driver_deinit();
}
