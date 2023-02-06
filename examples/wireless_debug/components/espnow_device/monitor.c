/* Wireless Debug Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#endif

#include "espnow.h"
#include "espnow_cmd.h"
#include "espnow_console.h"
#include "espnow_ctrl.h"
#include "espnow_log.h"
#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

#include "sdcard.h"

#include "mdns.h"
#include "lwip/apps/netbiosns.h"

#include "web_server.h"

static const char *TAG = "monitor";

static void app_wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static esp_err_t app_espnow_debug_recv_process(uint8_t *src_addr, void *data,
        size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    char *recv_data = (char *)data;
    web_server_send(src_addr, recv_data, size, rx_ctrl);

    printf("[" MACSTR "][%d][%d]: %.*s", MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi, size, recv_data);
    fflush(stdout);

    if (sdcard_is_mount()) {
        char file_name[32] = {0x0};
        sprintf(file_name, "%02x-%02x-%02x-%02x-%02x-%02x.log", MAC2STR(src_addr));
        size = (recv_data[size - 1] == '\0') ? size - 1 : size;
        sdcard_write_file(file_name, UINT32_MAX, recv_data, size);
    }


    return ESP_OK;
}

#if CONFIG_APP_WEB_SERVER

#define MDNS_SERVICE_NAME "espnow-webserver"

static int device_channel = 0;

static esp_err_t app_espnow_debug_recv_beacon(uint8_t *src_addr, void *data,
        size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    printf("[" MACSTR "][%d][%d]: %.*s", MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi, size, (char *)data);
    fflush(stdout);

    if (strstr(data, "beacon_func")) {
        device_channel = rx_ctrl->channel;
    }

    return ESP_OK;
}

static uint8_t app_find_device_channel(const uint8_t addr[ESPNOW_ADDR_LEN])
{
    esp_err_t ret = ESP_OK;
    char *data = "beacon";
    static wifi_country_t country = {0};

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
    };

    esp_wifi_get_country(&country);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DEBUG_LOG, true, app_espnow_debug_recv_beacon);
    device_channel = 0;

    for (int i = 0; i < country.nchan; ++i) {
        esp_wifi_set_channel(country.schan + i, WIFI_SECOND_CHAN_NONE);
        frame_head.channel = country.schan + i;

        ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_COMMAND, ESPNOW_ADDR_BROADCAST,
                          data, strlen(data) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        vTaskDelay(pdMS_TO_TICKS(100));
        if (device_channel) {
            break;
        }
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DEBUG_LOG, false, NULL);

    return device_channel;
}

static void app_wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

static void app_wifi_init_softap(uint8_t channel)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_APP_WIFI_SOFTAP_SSID,
            .ssid_len = strlen(CONFIG_APP_WIFI_SOFTAP_SSID),
            .channel = channel,
            .max_connection = 2,
        },
    };

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &app_wifi_event_handler, NULL));
    
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG, "\n========== Web Server Config ==========");
    ESP_LOGI(TAG, "SoftAP: ssid: %s", CONFIG_APP_WIFI_SOFTAP_SSID);
    ESP_LOGI(TAG, "Link: http://%s\n", CONFIG_APP_MDNS_HOST_NAME);
}

static esp_err_t app_init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_APP_WEB_MOUNT_POINT,
        .partition_label = "www",
        .max_files = 5,
        .format_if_mount_failed = false
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info("www", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

static esp_err_t app_web_command()
{
    uint8_t channel = 0;

    do {
        channel = app_find_device_channel(ESPNOW_ADDR_BROADCAST);

        if(!channel) {
            ESP_LOGW(TAG, "No esp-now device found");
        }
    } while (channel == 0);

    app_wifi_init_softap(channel);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "wireless_debug"},
        {"path", "/"}
    };

    mdns_init();
    mdns_hostname_set(CONFIG_APP_MDNS_HOST_NAME);
    mdns_instance_name_set("Wireless ESP-NOW Debug WebServer");
    ESP_ERROR_CHECK(mdns_service_add("ESPNOW-WebServer", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
    netbiosns_init();

    ESP_ERROR_CHECK(app_init_fs());
    ESP_ERROR_CHECK(web_server_start(CONFIG_APP_WEB_MOUNT_POINT));

    return ESP_OK;
}

#endif  /**< CONFIG_APP_WEB_SERVER */

void app_espnow_monitor_device_start()
{
    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);

    espnow_storage_init();

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.qsize = 128;
    espnow_init(&espnow_config);

    /**
     * @brief Add debug function, you can use serial command and wireless debugging
     */
    espnow_console_config_t console_config = {
        .monitor_command = {
            .uart   = true,
        },
        .store_history = {
            .base_path = "/spiffs",
            .partition_label = "console"
        },
    };

    espnow_console_init(&console_config);
    espnow_console_commands_register();

#if CONFIG_APP_WEB_SERVER
    app_web_command();
#endif /**< CONFIG_APP_WEB_SERVER */

    /** Receive esp-now data from other device */
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DEBUG_LOG, true, app_espnow_debug_recv_process);
}