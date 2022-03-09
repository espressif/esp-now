/* Debug Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_utils.h"
#include "esp_storage.h"
#include "espnow.h"
#include "espnow_log.h"
#include "espnow_console.h"

#include "protocol_examples_common.h"

static const char *TAG = "app_main";

/**
 * @brief http_client task creation
 *      You can modify the http address according to your needs,
 *      parameters CONFIG_FLASH_LOG_POST_URL if change.and parameters
 *      transport_type if change.
 */
static void log_read_task(void *arg)
{
    esp_err_t ret   = ESP_OK;
    char *log_data  = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t log_size = 0;
    esp_http_client_handle_t client  = NULL;
    esp_http_client_config_t config  = {
        .url            = CONFIG_FLASH_LOG_POST_URL,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
    };

    client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        log_size = espnow_log_flash_size();
        ESP_ERROR_CONTINUE(!log_size, "");

        ret = esp_http_client_open(client, log_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
            esp_http_client_close(client);
            continue;
        }

        /**
         * @brief Read the log data and transfer the data out via http server
         */
        for (size_t size = MIN(ESPNOW_DATA_LEN, log_size);
                size > 0 && espnow_log_flash_read(log_data, &size) == ESP_OK;
                log_size -= size, size = MIN(ESPNOW_DATA_LEN, log_size)) {
            ret = esp_http_client_write(client, log_data, size);
            ESP_ERROR_BREAK(ret < 0, "<%s> Failed to write HTTP data", esp_err_to_name(ret));
        }

        esp_http_client_close(client);
    }

    ESP_FREE(log_data);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/**
 * @brief Register a console command that outputs system information
 */
static struct {
    struct arg_lit *info;
    struct arg_end *end;
} console_system_info_args;

static int console_system_info(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &console_system_info_args) != ESP_OK) {
        arg_print_errors(stderr, console_system_info_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (console_system_info_args.info->count) {
        uint8_t primary    = 0;
        uint8_t sta_mac[6] = {0};
        wifi_second_chan_t second = 0;

        esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
        esp_wifi_get_channel(&primary, &second);

        ESP_LOGI(TAG, "System information, mac: " MACSTR ", channel: %d, free heap: %u",
                 MAC2STR(sta_mac), primary, esp_get_free_heap_size());
    }

    return ESP_OK;
}

static void console_register_system_info()
{
    console_system_info_args.info = arg_lit0("i", "info", "Print the system information");
    console_system_info_args.end  = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "system_info",
        .help = "Print the system information",
        .hint = NULL,
        .func = console_system_info,
        .argtable = &console_system_info_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void espnow_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (id) {
        case ESP_EVENT_ESPNOW_LOG_FLASH_FULL: {
            ESP_LOGI(TAG, "The flash partition that stores the log is full, size: %d", espnow_log_flash_size());
            break;
        }

        default:
            break;
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /**< Initialize time synchronization */
    esp_timesync_start();

    /**
     * @brief ESPNOW init
     */
    esp_storage_init();
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
    esp_log_level_set("*", ESP_LOG_INFO);

    /**
     * @brief Register a console command that outputs system information
     */
    console_register_system_info();

    ESP_LOGI(TAG, " ==================================================");
    ESP_LOGI(TAG, " |             Steps to test ESP-info             |");
    ESP_LOGI(TAG, " |                                                |");
    ESP_LOGI(TAG, " |  1. Print 'help' to gain overview of commands  |");
    ESP_LOGI(TAG, " |     esp32c3> help                                  |");
    ESP_LOGI(TAG, " |  2. System information                         |");
    ESP_LOGI(TAG, " |     esp32c3> system_info -i                       |");
    ESP_LOGI(TAG, " |                                                |");
    ESP_LOGI(TAG, " ==================================================\n");

    xTaskCreate(log_read_task, "log_read_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
}
