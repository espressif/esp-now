/* Light Example

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
#include "light_driver.h"

#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_ctrl.h"
#include "espnow_console.h"
#include "espnow_log.h"

static const char *TAG = "app_main";

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, 
                    WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
}

static void espnow_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    if(base != ESP_EVENT_ESPNOW) {
        return ;
    }

    switch (id) {
        case ESP_EVENT_ESPNOW_CTRL_BIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "band, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            light_driver_breath_start(0, 255, 0); /**< green blink */
            vTaskDelay(pdMS_TO_TICKS(3000));
            light_driver_breath_stop();
            break;
        }

        case ESP_EVENT_ESPNOW_CTRL_UNBIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "unband, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            light_driver_breath_start(128, 128, 0); /**< yellow blink */
            vTaskDelay(pdMS_TO_TICKS(3000));
            light_driver_breath_stop();
            break;
        }

        case ESP_EVENT_ESPNOW_OTA_STARTED: {
            ESP_LOGI(TAG, "ESP_EVENT_ESPNOW_OTA_STARTED");
            light_driver_breath_start(0, 0, 5); /**< blue blink */
            break;
        }

        case ESP_EVENT_ESPNOW_OTA_STATUS: {
            uint32_t *progress = (uint32_t *)event_data;
            ESP_LOGI(TAG, "progress: %d", *progress);
            light_driver_breath_start(0, 0, 255 * (*progress) / 100); /**< blue blink */
            break;
        }

        case ESP_EVENT_ESPNOW_OTA_FINISH: {
            ESP_LOGI(TAG, "ESP_EVENT_ESPNOW_OTA_FINISH");
            light_driver_breath_start(0, 255, 0); /**< green blink */
            esp_reboot(pdMS_TO_TICKS(3000));
            break;
        }

        default:
            break;
    }
}

void app_main()
{
    esp_storage_init();
    esp_event_loop_create_default();

    /**
     * @brief Driver init
     */
    light_driver_config_t driver_cfg = COFNIG_LIGHT_TYPE_DEFAULT();
    light_driver_init(&driver_cfg);
    light_driver_set_switch(true);

    /**
     * @brief ESPNOW init
     */
    wifi_init();
    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, espnow_event_handler, NULL);

    /**
     * @brief Debug
     */
    espnow_console_config_t console_config = {
        .monitor_command.uart   = true,
        .monitor_command.espnow = true,
    };
    espnow_log_config_t log_config = {
        .log_level_uart    = ESP_LOG_INFO,
        .log_level_espnow  = ESP_LOG_INFO,
        .log_level_flash   = ESP_LOG_INFO,
    };

    espnow_console_init(&console_config);
    espnow_console_commands_register();
    espnow_log_init(&log_config);
    esp_log_level_set("*", ESP_LOG_DEBUG);

    /**
     * @brief OTA
     */
    espnow_ota_config_t ota_config = {
        .skip_version_check       = true,
        .progress_report_interval = 10,
    };
    espnow_ota_responder_start(&ota_config);

    /**
     * @brief Control
     */
    espnow_ctrl_responder_bind(30 * 1000, -45, NULL);

    espnow_attribute_t initiator_attribute;
    espnow_attribute_t responder_attribute;
    uint32_t responder_value;

    while (espnow_ctrl_responder_recv(&initiator_attribute, &responder_attribute, &responder_value) == ESP_OK) {
        ESP_LOGI(TAG, "espnow_ctrl_responder_recv, initiator_attribute: %d, responder_attribute: %d, value: %d",
                 initiator_attribute, responder_attribute, responder_value);

        switch (responder_attribute) {
            case ESPNOW_ATTRIBUTE_POWER_ADD:
                responder_value = light_driver_get_switch() + responder_value;
                break;

            case ESPNOW_ATTRIBUTE_POWER:
                light_driver_set_switch((light_driver_get_value() > 0) ? responder_value & 0x1 : 1);
                break;

            case ESPNOW_ATTRIBUTE_HUE_ADD:
                responder_value = light_driver_get_hue() + responder_value;

            case ESPNOW_ATTRIBUTE_HUE:
                light_driver_set_hue(responder_value > 360 ? 0 : responder_value);
                light_driver_set_saturation(responder_value > 360 ? 0 : 100);
                break;

            case ESPNOW_ATTRIBUTE_SATURATION_ADD:
                responder_value = light_driver_get_saturation() + responder_value;

            case ESPNOW_ATTRIBUTE_SATURATION:
                light_driver_set_saturation(responder_value);
                break;

            case ESPNOW_ATTRIBUTE_BRIGHTNESS_ADD:
                responder_value = light_driver_get_value() + responder_value;

            case ESPNOW_ATTRIBUTE_BRIGHTNESS:
                light_driver_set_value(responder_value < 5 ? 5 : responder_value > 100 ? 100 : responder_value);
                break;

            default:
                break;
        }
    }
}
