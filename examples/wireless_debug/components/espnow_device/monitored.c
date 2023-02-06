/* Debug Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_http_client.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow.h"
#include "espnow_console.h"
#include "espnow_log.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

static const char *TAG = "monitored";

#define EXAMPLE_ESP_MAXIMUM_RETRY  5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void app_wifi_event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void app_wifi_init()
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, app_wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, app_wifi_event_handler, NULL);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#ifdef CONFIG_APP_POST_LOG_TO_HTTP_SERVER
/**
 * @brief http_client task creation
 *      You can modify the http address according to your needs,
 *      parameters CONFIG_FLASH_LOG_POST_URL if change.and parameters
 *      transport_type if change.
 */
static void app_log_read_task(void *arg)
{
    esp_err_t ret   = ESP_OK;
    char *log_data  = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t log_size = 0;
    esp_http_client_handle_t client  = NULL;
    esp_http_client_config_t config  = {
        .url            = CONFIG_APP_FLASH_LOG_POST_URL,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
    };

    client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
        * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               portMAX_DELAY);

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
        * happened. */
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGD(TAG, "connected to ap");
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGD(TAG, "Failed to connect to ap");
            continue;
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
            continue;
        }

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
#endif

/**
 * @brief Register a console command that outputs system information
 */
static struct {
    struct arg_lit *info;
    struct arg_end *end;
} app_console_system_info_args;

static int app_console_system_info(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &app_console_system_info_args) != ESP_OK) {
        arg_print_errors(stderr, app_console_system_info_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (app_console_system_info_args.info->count) {
        uint8_t primary    = 0;
        uint8_t sta_mac[6] = {0};
        wifi_second_chan_t second = 0;

        esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
        esp_wifi_get_channel(&primary, &second);

        ESP_LOGI(TAG, "System information, mac: " MACSTR ", channel: %" PRIu8 ", free heap: %" PRIu32 "",
                 MAC2STR(sta_mac), primary, esp_get_free_heap_size());
    }

    return ESP_OK;
}

static void app_console_register_system_info()
{
    app_console_system_info_args.info = arg_lit0("i", "info", "Print the system information");
    app_console_system_info_args.end  = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "system_info",
        .help = "Print the system information",
        .hint = NULL,
        .func = app_console_system_info,
        .argtable = &app_console_system_info_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void app_espnow_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
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

void app_espnow_monitored_device_start()
{
    espnow_storage_init();
    app_wifi_init();

    /**< Initialize time synchronization */
    espnow_timesync_start();

    /**
     * @brief ESPNOW init
     */
    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, app_espnow_event_handler, NULL);

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
    app_console_register_system_info();

    ESP_LOGI(TAG, " ==================================================");
    ESP_LOGI(TAG, " |             Steps to test ESP-info             |");
    ESP_LOGI(TAG, " |                                                |");
    ESP_LOGI(TAG, " |  1. Print 'help' to gain overview of commands  |");
    ESP_LOGI(TAG, " |     esp32c3> help                                  |");
    ESP_LOGI(TAG, " |  2. System information                         |");
    ESP_LOGI(TAG, " |     esp32c3> system_info -i                       |");
    ESP_LOGI(TAG, " |                                                |");
    ESP_LOGI(TAG, " ==================================================\n");

#ifdef CONFIG_APP_POST_LOG_TO_HTTP_SERVER
    xTaskCreate(app_log_read_task, "log_read_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif
}