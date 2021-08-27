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

#include "driver/gpio.h"
#include "driver/rmt.h"
#include "led_strip.h"

#ifdef CONFIG_IDF_TARGET_ESP32C3
#define BOOT_KEY_GPIIO        GPIO_NUM_9
#define CONFIG_LED_STRIP_GPIO GPIO_NUM_8
#else
#define BOOT_KEY_GPIIO        GPIO_NUM_0
#define CONFIG_LED_STRIP_GPIO GPIO_NUM_18
#endif

#define BOOT_KEY_PRESS_TIMEOUT_BIND    1000
#define BOOT_KEY_PRESS_TIMEOUT_UNBIND  5000

static const char *TAG = "main";
static xQueueHandle gpio_evt_queue = NULL;
static led_strip_t *g_strip_handle = NULL;

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

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void initiator_key_task(void *arg)
{
    ESP_LOGI(TAG, "Initiator key handle task is running");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = BIT(BOOT_KEY_GPIIO),
        .pull_up_en = true,
    };

    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOOT_KEY_GPIIO, gpio_isr_handler, (void *) BOOT_KEY_GPIIO);

    uint32_t gpio_num;
    uint32_t last_press_timetamp = UINT32_MAX;
    gpio_evt_queue = xQueueCreate(1, sizeof(uint32_t));
    bool status = 0;

    while (xQueueReceive(gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
        bool key_level = gpio_get_level(gpio_num);
        vTaskDelay(pdMS_TO_TICKS(10));

        if (key_level != gpio_get_level(gpio_num)) {
            continue;
        }

        if (key_level == 1 && last_press_timetamp < esp_timer_get_time() / 1000) {
            if (esp_timer_get_time() / 1000 - last_press_timetamp < BOOT_KEY_PRESS_TIMEOUT_BIND) {
                ESP_LOGI(TAG, "espnow_ctrl_initiator_send, status: %d", status);
                espnow_ctrl_initiator_send(ESPNOW_ATTRIBUTE_KEY_1, ESPNOW_ATTRIBUTE_POWER, status);
                status = !status;
            } else if (esp_timer_get_time() / 1000 - last_press_timetamp < BOOT_KEY_PRESS_TIMEOUT_UNBIND) {
                ESP_LOGI(TAG, "espnow_ctrl_initiator_bind");
                espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
            } else {
                ESP_LOGI(TAG, "espnow_ctrl_initiator_unbind");
                espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, false);
            }
        }

        last_press_timetamp = (key_level == 0) ? esp_timer_get_time() / 1000 : UINT32_MAX;
    }

    ESP_LOGW(TAG, "Initiator key task is exit");

    vTaskDelete(NULL);
}


static void responder_light_task(void *arg)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(CONFIG_LED_STRIP_GPIO, RMT_CHANNEL_0);
    // set counter clock to 40MHz
    config.clk_div = 2;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    // install ws2812 driver
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(1, (led_strip_dev_t)config.channel);
    g_strip_handle = led_strip_new_rmt_ws2812(&strip_config);
    // Clear LED strip (turn on all LEDs)
    g_strip_handle->set_pixel(g_strip_handle, 0, 0x88, 0x88, 0x88);
    ESP_ERROR_CHECK(g_strip_handle->clear(g_strip_handle, 100));

    espnow_attribute_t initiator_attribute;
    espnow_attribute_t responder_attribute;
    uint32_t responder_value;

    espnow_ctrl_responder_bind(30 * 1000, -55, NULL);

    while (espnow_ctrl_responder_recv(&initiator_attribute, &responder_attribute, &responder_value) == ESP_OK) {
        ESP_LOGI(TAG, "espnow_ctrl_responder_recv, initiator_attribute: %d, responder_attribute: %d, value: %d",
                 initiator_attribute, responder_attribute, responder_value);

        switch (responder_attribute) {
            case ESPNOW_ATTRIBUTE_POWER:
                g_strip_handle->set_pixel(g_strip_handle, 0, (responder_value ? 0x88 : 0x00), (responder_value ? 0x88 : 0x00), (responder_value ? 0x88 : 0x00));
                g_strip_handle->refresh(g_strip_handle, 100);
                break;

            default:
                break;
        }
    }

    ESP_LOGW(TAG, "Responder light task is exit");

    vTaskDelete(NULL);
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
            
            g_strip_handle->set_pixel(g_strip_handle, 0, 0x0, 0x88, 0x0);
            ESP_ERROR_CHECK(g_strip_handle->clear(g_strip_handle, 100));
            break;
        }

        case ESP_EVENT_ESPNOW_CTRL_UNBIND: {
            espnow_ctrl_bind_info_t *info = (espnow_ctrl_bind_info_t *)event_data;
            ESP_LOGI(TAG, "unband, uuid: " MACSTR ", initiator_type: %d", MAC2STR(info->mac), info->initiator_attribute);
            
            g_strip_handle->set_pixel(g_strip_handle, 0, 0x88, 0x0, 0x00);
            ESP_ERROR_CHECK(g_strip_handle->clear(g_strip_handle, 100));
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

    xTaskCreate(initiator_key_task, "initiator_key_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(responder_light_task, "responder_light_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
}
