#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
// #include "esp_neif_type.h"

#include "esp_netif.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
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
#include "esp_sleep.h"
#include "esp32/rom/rtc.h"
#include "driver/rtc_io.h"

#include "esp_utils.h"
#include "esp_storage.h"

#include "led_pwm.h"
#include "button_driver.h"
#include "espnow_ctrl.h"

#define HUMAN_INFRARED_GPIO_NUM     GPIO_NUM_25
#define BUTTON_IO_NUM               GPIO_NUM_39

#define config_mode_flag = false;
static const char *TAG = "human_infrared";

enum light_channel {
    CHANNEL_ID_RED   = 0,
};

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}


void bind_key_task(void *arg)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_event_loop_create_default();
    esp_storage_init();
    
    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = BIT64(BUTTON_IO_NUM),
        .pull_up_en = true,
    };

    //create a queue to handle gpio event from isr
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_IO_NUM, gpio_isr_handler, (void *) BUTTON_IO_NUM);
    gpio_evt_queue = xQueueCreate(1, sizeof(uint32_t));
    uint32_t gpio_num;
    uint32_t last_press_timetamp = UINT32_MAX;

    while (xQueueReceive(gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
        bool key_level = gpio_get_level(gpio_num);
        vTaskDelay(pdMS_TO_TICKS(10));

        if (key_level != gpio_get_level(gpio_num)) {
            continue;
        }

        if (key_level == 1 && last_press_timetamp < esp_timer_get_time() / 1000) {
            if (esp_timer_get_time() / 1000 - last_press_timetamp < 1000) {
                ESP_LOGI(TAG, "espnow_ctrl_initiator_bind");
                espnow_ctrl_initiator_bind(ESPNOW_BUTTON_ATTRIBUTE, true);
            } else {
                ESP_LOGI(TAG, "espnow_ctrl_initiator_unbind");
                espnow_ctrl_initiator_bind(ESPNOW_BUTTON_ATTRIBUTE, false);
            }
        }

        last_press_timetamp = (key_level == 0) ? esp_timer_get_time() / 1000 : UINT32_MAX;
    }
}


void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("wifi", ESP_LOG_WARN);

    led_pwm_init(LEDC_TIMER_0, LEDC_LOW_SPEED_MODE, 1000);
    led_pwm_regist_channel(CHANNEL_ID_RED, GPIO_NUM_15);

    ESP_ERROR_CHECK(rtc_gpio_init(HUMAN_INFRARED_GPIO_NUM));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(HUMAN_INFRARED_GPIO_NUM, RTC_GPIO_MODE_INPUT_ONLY));

    esp_storage_init();
    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

    xTaskCreate(bind_key_task, "bind_key", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);

    bool last_level = 0;
    bool status = 0;
    uint32_t timetamp = 0;

    while (true) {
        uint32_t count = 0;
        bool level = rtc_gpio_get_level(HUMAN_INFRARED_GPIO_NUM);

        if (last_level != level) {
            if(level == 1) {
                timetamp = esp_timer_get_time();
                status = level;
                ESP_LOGI(TAG, "espnow_ctrl_initiator_send, status: %d", status);
                espnow_ctrl_initiator_send(ESPNOW_BUTTON_ATTRIBUTE, ESPNOW_ATTRIBUTE_POWER, status);
            }
        }

        if(level == 1) {
            timetamp = esp_timer_get_time();
        }

        if (level == 0 && timetamp && esp_timer_get_time() - timetamp > 5 * 1000 *1000) {
            timetamp = 0;
            status = level;
            ESP_LOGI(TAG, "espnow_ctrl_initiator_send, status: %d", status);
            espnow_ctrl_initiator_send(ESPNOW_BUTTON_ATTRIBUTE, ESPNOW_ATTRIBUTE_POWER, status);
        }

        last_level = level;

        ESP_LOGW(TAG, "level : %d, count: %d, timetamp: %d, %lld", level, count, timetamp, esp_timer_get_time());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
