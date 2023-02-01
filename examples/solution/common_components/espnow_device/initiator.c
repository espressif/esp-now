/* Solution Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"

#ifdef CONFIG_ESPNOW_INITIATOR
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#endif

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_utils.h"
#include "esp_storage.h"
#include "espnow.h"

#ifdef CONFIG_ESPNOW_DEBUG
#include "espnow_log.h"
#include "espnow_console.h"
#include "sdcard.h"
#include "esp_spiffs.h"
#endif
#ifdef CONFIG_ESPNOW_OTA
#include "espnow_ota.h"
#endif
#ifdef CONFIG_ESPNOW_PROVISION
#include "espnow_prov.h"
#endif
#ifdef CONFIG_ESPNOW_SECURITY
#include "espnow_security.h"
#include "espnow_security_handshake.h"
#endif

static const char *TAG = "init";

static void espnow_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    if (base != ESP_EVENT_ESPNOW) {
        return;
    }

    switch (id) {
        default:
        break;
    }
}

/* 
 * May connecting network by debug command or espnow provisioning
 */
#if defined(CONFIG_ESPNOW_DEBUG) || defined(CONFIG_ESPNOW_PROVISION)
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
#endif

#ifdef CONFIG_ESPNOW_DEBUG
static esp_err_t espnow_debug_recv_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    char *recv_data = (char *)data;
    // TODO
    // web_server_send(src_addr, recv_data, size, rx_ctrl);

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
#endif

#ifdef CONFIG_ESPNOW_PROVISION
static esp_err_t responder_recv_callback(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_prov_initator_t *initator_info = (espnow_prov_initator_t *)data;
    /**
     * @brief Authenticate the device through the information of the initiator
     */
    ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s, Auth Mode: %d, device_secret: %s",
                MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi,
                initator_info->product_id, initator_info->device_name,
                initator_info->auth_mode, initator_info->device_secret);

    return ESP_OK;
}

esp_err_t provision_beacon_start(int32_t sec)
{
    wifi_config_t wifi_config = { 0 };
    espnow_prov_wifi_t prov_wifi_config = { 0 };
    espnow_prov_responder_t responder_info = {
        .product_id = "responder_test"
    };

    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get wifi config failed, %d", ret);
        return ret;
    }

    if (strlen((const char *) wifi_config.sta.ssid) == 0) {
        ESP_LOGW(TAG, "WiFi not configured");
        return ESP_FAIL;
    }

    memcpy(&prov_wifi_config.sta, &wifi_config.sta, sizeof(wifi_sta_config_t));

    ret = espnow_prov_responder_start(&responder_info, pdMS_TO_TICKS(sec * 1000), &prov_wifi_config, responder_recv_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "espnow_prov_responder_start failed, %d", ret);
    }

    return ret;
}

static void provisioning_responder(void *arg)
{    
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

        if (provision_beacon_start(30) != ESP_OK) {
            break;
        }

        // Clear WIFI_CONNECTED_BIT to restart responder when reconnection
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

    ESP_LOGI(TAG, "provisioning responder exit");
    vTaskDelete(NULL);
}
#endif

#ifdef CONFIG_ESPNOW_SECURITY
const char *pop_data = CONFIG_ESPNOW_SESSION_POP;
static TaskHandle_t s_sec_task;

void initiator_sec(void *arg)
{
    espnow_sec_result_t espnow_sec_result = {0};
    espnow_sec_responder_t *info_list = NULL;
    espnow_addr_t *dest_addr_list = NULL;
    size_t num = 0;
    uint8_t key_info[APP_KEY_LEN];

    if (espnow_get_key(key_info) != ESP_OK) {
        esp_fill_random(key_info, APP_KEY_LEN);
    }
    espnow_set_key(key_info);

    uint32_t start_time1 = xTaskGetTickCount();
    espnow_sec_initiator_scan(&info_list, &num, pdMS_TO_TICKS(3000));
    ESP_LOGW(TAG, "espnow wait security num: %u", num);

    if (num == 0) {
        goto EXIT;
    }

    dest_addr_list = ESP_MALLOC(num * ESPNOW_ADDR_LEN);

    for (size_t i = 0; i < num; i++) {
        memcpy(dest_addr_list[i], info_list[i].mac, ESPNOW_ADDR_LEN);
    }

    espnow_sec_initiator_scan_result_free();

    uint32_t start_time2 = xTaskGetTickCount();
    esp_err_t ret = espnow_sec_initiator_start(key_info, pop_data, dest_addr_list, num, &espnow_sec_result);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> espnow_sec_initator_start", esp_err_to_name(ret));

    ESP_LOGI(TAG, "App key is sent to the device to complete, Spend time: %" PRId32" ms, Scan time: %" PRId32 "ms",
             (xTaskGetTickCount() - start_time1) * portTICK_PERIOD_MS, 
             (start_time2 - start_time1) * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Devices security completed, successed_num: %u, unfinished_num: %u", 
             espnow_sec_result.successed_num, espnow_sec_result.unfinished_num);

EXIT:
    ESP_FREE(dest_addr_list);
    espnow_sec_initator_result_free(&espnow_sec_result);

    vTaskDelete(NULL);
    s_sec_task = NULL;
}

void initiator_sec_start(void)
{
    if (!s_sec_task) {
        xTaskCreate(initiator_sec, "sec", 3072, NULL, tskIDLE_PRIORITY + 1, &s_sec_task);
    }
}
#endif

void espnow_initiator_reg()
{
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, espnow_event_handler, NULL);

#if defined(CONFIG_ESPNOW_DEBUG) || defined(CONFIG_ESPNOW_PROVISION)
    s_wifi_event_group = xEventGroupCreate();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);
#endif
}

void espnow_initiator()
{
#ifdef CONFIG_ESPNOW_SECURITY
    initiator_sec_start();
#endif

#ifdef CONFIG_ESPNOW_DEBUG
    espnow_console_config_t console_config = {
        .monitor_command.uart   = true,
        .store_history = {
            .base_path = "/spiffs",
            .partition_label = "console"
        },
    };

    espnow_console_init(&console_config);
    espnow_console_commands_register();

#ifdef CONFIG_EXAMPLE_WEB_SERVER
    web_command();
#endif
    /** Receive esp-now data from other device */
    espnow_set_type(ESPNOW_TYPE_DEBUG_LOG, 1, espnow_debug_recv_process);
#endif

#ifdef CONFIG_ESPNOW_OTA
    // OTA with command
#endif

#ifdef CONFIG_ESPNOW_PROVISION
    xTaskCreate(provisioning_responder, "PROV_resp", 3072, NULL, tskIDLE_PRIORITY + 1, NULL);
#endif
}
#endif