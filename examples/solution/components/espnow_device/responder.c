/* Solution Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

#ifdef CONFIG_APP_ESPNOW_DEBUG
#include "espnow_log.h"
#include "espnow_console.h"
#include "esp_http_client.h"
#endif

#ifdef CONFIG_APP_ESPNOW_OTA
#include "espnow_ota.h"
#endif

#ifdef CONFIG_APP_ESPNOW_PROVISION
#include "espnow_prov.h"
#endif

#ifdef CONFIG_APP_ESPNOW_SECURITY
#include "espnow_security.h"
#include "espnow_security_handshake.h"
#endif

static const char *TAG = "resp";

static void app_espnow_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base != ESP_EVENT_ESPNOW) {
        return;
    }

    switch (id) {
#ifdef CONFIG_APP_ESPNOW_DEBUG
    case ESP_EVENT_ESPNOW_LOG_FLASH_FULL: {
        ESP_LOGI(TAG, "The flash partition that stores the log is full, size: %d", espnow_log_flash_size());
        break;
    }
#endif

    default:
        break;
    }
}

/*
 * Connect to route by debug command or espnow provisioning
 */
#if defined(CONFIG_APP_ESPNOW_DEBUG) || defined(CONFIG_APP_ESPNOW_PROVISION)
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

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
#endif

#if defined(CONFIG_APP_ESPNOW_DEBUG) && defined(CONFIG_APP_POST_LOG_TO_HTTP_SERVER)
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

#ifdef CONFIG_APP_ESPNOW_PROVISION
static TaskHandle_t s_prov_task;

static esp_err_t app_espnow_prov_initiator_recv_cb(uint8_t *src_addr, void *data,
        size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_prov_wifi_t *wifi_config = (espnow_prov_wifi_t *)data;

    ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, wifi_mode: %d, ssid: %s, password: %s, token: %s",
             MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi,
             wifi_config->mode, wifi_config->sta.ssid, wifi_config->sta.password, wifi_config->token);

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, (wifi_config_t *)&wifi_config->sta));
    ESP_ERROR_CHECK(esp_wifi_connect());
    return ESP_OK;
}

static void app_espnow_prov_initiator_init(void *arg)
{
    esp_err_t ret = ESP_OK;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    espnow_prov_initiator_t initiator_info = {
        .product_id = "initiator_test",
    };
    espnow_addr_t responder_addr = {0};
    espnow_prov_responder_t responder_info = {0};

    for (;;) {
#ifdef CONFIG_APP_ESPNOW_SECURITY
        uint8_t key_info[APP_KEY_LEN];

        /* Security key is not ready */
        if (espnow_get_key(key_info) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
#endif

        ret = espnow_prov_initiator_scan(responder_addr, &responder_info, &rx_ctrl, pdMS_TO_TICKS(3 * 1000));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "");

        ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s",
                 MAC2STR(responder_addr), rx_ctrl.channel, rx_ctrl.rssi,
                 responder_info.product_id, responder_info.device_name);

        ret = espnow_prov_initiator_send(responder_addr, &initiator_info, app_espnow_prov_initiator_recv_cb, pdMS_TO_TICKS(3 * 1000));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_prov_responder_add", esp_err_to_name(ret));

        break;
    }

    ESP_LOGI(TAG, "provisioning initiator exit");
    vTaskDelete(NULL);
    s_prov_task = NULL;
}

esp_err_t app_espnow_prov_responder_start(void)
{
    if (!s_prov_task) {
        xTaskCreate(app_espnow_prov_initiator_init, "PROV_init", 3072, NULL, tskIDLE_PRIORITY + 1, &s_prov_task);
    }

    return ESP_OK;
}
#endif

void app_espnow_responder_register()
{
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, app_espnow_event_handler, NULL);

#if defined(CONFIG_APP_ESPNOW_DEBUG) || defined(CONFIG_APP_ESPNOW_PROVISION)
    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, app_wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, app_wifi_event_handler, NULL);
#endif
}

void app_espnow_responder()
{
#ifdef CONFIG_APP_ESPNOW_SECURITY
    const char *pop_data = CONFIG_APP_ESPNOW_SESSION_POP;
    uint8_t key_info[APP_KEY_LEN];

    /* If espnow_set_key succeed, sending and receiving will be in security mode */
    if (espnow_get_key(key_info) == ESP_OK) {
        espnow_set_key(key_info);
        espnow_set_dec_key(key_info);
    }

    /* If responder handshake with initiator succeed, espnow_set_key will be executed again. */
    espnow_sec_responder_start(pop_data);
#endif

#ifdef CONFIG_APP_ESPNOW_DEBUG
    /**< Initialize time synchronization */
    espnow_timesync_start();

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

#ifdef CONFIG_APP_POST_LOG_TO_HTTP_SERVER
    xTaskCreate(app_log_read_task, "log_read_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif
#endif

#ifdef CONFIG_APP_ESPNOW_OTA
    espnow_ota_config_t ota_config = {
        .skip_version_check       = true,
        .progress_report_interval = 10,
    };

    espnow_ota_responder_start(&ota_config);
#endif
}