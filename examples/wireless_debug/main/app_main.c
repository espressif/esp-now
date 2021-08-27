/* Wireless Debug Example

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
#include "espnow.h"

#include "espnow_console.h"
#include "espnow_log.h"

#include "debug_cmd.h"
#include "sdcard.h"

#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_ctrl.h"

#include "esp_spiffs.h"

#include "mdns.h"
#include "lwip/apps/netbiosns.h"

#include "web_server.h"
#include "cmd_system.h"

static const char *TAG = "app_main";

static void wifi_init()
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

static void espnow_debug_recv_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    uint8_t src_addr[6]  = { 0 };
    char *data   = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t size    = 0;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    for (;;) {
        ret = espnow_recv(ESPNOW_TYPE_DEBUG_LOG, src_addr, data, &size, &rx_ctrl, pdMS_TO_TICKS(1000));
        ESP_ERROR_CONTINUE(ret == ESP_ERR_TIMEOUT, "");
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_recv, ret: %x, err_name: %s", ret, esp_err_to_name(ret));

        web_server_send(src_addr, data, size, &rx_ctrl);

        printf("[" MACSTR "][%d][%d]: %.*s", MAC2STR(src_addr), rx_ctrl.channel, rx_ctrl.rssi, size, data);
        fflush(stdout);

        if (sdcard_is_mount()) {
            char file_name[32] = {0x0};
            sprintf(file_name, "%02x-%02x-%02x-%02x-%02x-%02x.log", MAC2STR(src_addr));
            size = (data[size - 1] == '\0') ? size - 1 : size;
            sdcard_write_file(file_name, UINT32_MAX, data, size);
        }
    }

    free(data);
    vTaskDelete(NULL);
}

#if CONFIG_EXAMPLE_WEB_SERVER

#define MDNS_SERVICE_NAME "espnow-webserver"

static uint8_t find_device_channel(const uint8_t addr[ESPNOW_ADDR_LEN])
{
    esp_err_t ret = ESP_OK;
    char *data = "beacon";
    static wifi_country_t country = {0};
    size_t recv_size = 0;
    uint8_t src_addr[ESPNOW_ADDR_LEN] = {0};
    char *recv_data = ESP_MALLOC(ESPNOW_DATA_LEN);
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
    };

    esp_wifi_get_country(&country);

    for (int i = 0; i < country.nchan; ++i) {
        esp_wifi_set_channel(country.schan + i, WIFI_SECOND_CHAN_NONE);
        frame_head.channel = country.schan + i;

        ret = espnow_send(ESPNOW_TYPE_DEBUG_COMMAND, ESPNOW_ADDR_BROADCAST,
                          data, strlen(data) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        while (espnow_recv(ESPNOW_TYPE_DEBUG_LOG, src_addr, recv_data, &recv_size, &rx_ctrl, pdMS_TO_TICKS(100)) == ESP_OK) {
            printf("[" MACSTR "][%d][%d]: %.*s\n", MAC2STR(src_addr), rx_ctrl.channel, rx_ctrl.rssi, recv_size, recv_data);
            fflush(stdout);
        }

        if (strstr(recv_data, "beacon_func")) {
            break;
        }
    }

    ESP_FREE(recv_data);
    return rx_ctrl.channel;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
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

static void wifi_init_softap(uint8_t channel)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = CONFIG_EXAMPLE_WIFI_SOFTAP_SSID,
            .ssid_len = strlen(CONFIG_EXAMPLE_WIFI_SOFTAP_SSID),
            .channel = channel,
            .max_connection = 1,
        },
    };

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG, "\n========== Web Server Config ==========");
    ESP_LOGI(TAG, "SoftAP: ssid: %s", CONFIG_EXAMPLE_WIFI_SOFTAP_SSID);
    ESP_LOGI(TAG, "Link: http://%s\n", CONFIG_EXAMPLE_MDNS_HOST_NAME);
}

static esp_err_t init_fs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_EXAMPLE_WEB_MOUNT_POINT,
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

static esp_err_t web_command()
{
    uint8_t channel = 0;

    do {
        channel = find_device_channel(ESPNOW_ADDR_BROADCAST);

        if(!channel) {
            ESP_LOGW(TAG, "No esp-now device found");
        }
    } while (channel == 0);

    wifi_init_softap(channel);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "wireless_debug"},
        {"path", "/"}
    };

    mdns_init();
    mdns_hostname_set(CONFIG_EXAMPLE_MDNS_HOST_NAME);
    mdns_instance_name_set("Wireless ESP-NOW Debug WebServer");
    ESP_ERROR_CHECK(mdns_service_add("ESPNOW-WebServer", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
    netbiosns_init();

    ESP_ERROR_CHECK(init_fs());
    ESP_ERROR_CHECK(web_server_start(CONFIG_EXAMPLE_WEB_MOUNT_POINT));

    return ESP_OK;
}

#endif  /**< CONFIG_EXAMPLE_WEB_SERVER */

void app_main()
{
    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_storage_init();
    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.qsize.debug_log = 128;
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

    sdcard_config_t sdcard_config = {
        .gpio_num_cmd = GPIO_NUM_15,
        .gpio_num_d0  = GPIO_NUM_2,
        .gpio_num_d1  = GPIO_NUM_4,
        .gpio_num_d2  = GPIO_NUM_12,
        .gpio_num_d3  = GPIO_NUM_13,
    };

    /** Initializing SD card via SDMMC peripheral */
    sdcard_init(&sdcard_config);

    /** Register some commands */
    if (sdcard_is_mount()) {
        register_sdcard();
        register_wifi_sniffer();
    }

    register_system_common();
    register_wifi_config();
    register_wifi_scan();
    register_wifi_ping();
    register_scan();
    register_command();
    register_provisioning();
    register_control();
    register_ota();

    register_espnow_config();
    register_espnow_iperf();

#if CONFIG_EXAMPLE_WEB_SERVER
    web_command();
#endif /**< CONFIG_EXAMPLE_WEB_SERVER */

    /** Create esp-now receive task to listen esp-now data from other device */
    xTaskCreate(espnow_debug_recv_task, "espnow_debug_recv", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
}
