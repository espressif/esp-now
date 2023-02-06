/* OTA Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "espnow.h"
#include "espnow_ota.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

#include "protocol_examples_common.h"

static const char *TAG = "app_main";

#ifdef CONFIG_APP_ESPNOW_OTA_INITIATOR
static size_t app_firmware_download(const char *url)
{
#define OTA_DATA_PAYLOAD_LEN 1024

    esp_err_t ret       = ESP_OK;
    esp_ota_handle_t ota_handle = 0;
    uint8_t *data       = ESP_MALLOC(OTA_DATA_PAYLOAD_LEN);
    size_t total_size   = 0;
    uint32_t start_time = xTaskGetTickCount();

    esp_http_client_config_t config = {
        .url            = url,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
    };

    /**
     * @brief 1. Connect to the server
     */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_ERROR_GOTO(!client, EXIT, "Initialise HTTP connection");

    ESP_LOGI(TAG, "Open HTTP connection: %s", url);

    /**
     * @brief First, the firmware is obtained from the http server and stored
     */
    do {
        ret = esp_http_client_open(client, 0);

        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "<%s> Connection service failed", esp_err_to_name(ret));
        }
    } while (ret != ESP_OK);

    total_size = esp_http_client_fetch_headers(client);

    if (total_size <= 0) {
        ESP_LOGW(TAG, "Please check the address of the server");
        ret = esp_http_client_read(client, (char *)data, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(ret < 0, EXIT, "<%s> Read data from http stream", esp_err_to_name(ret));

        ESP_LOGW(TAG, "Recv data: %.*s", ret, data);
        goto EXIT;
    }

    /**
     * @brief 2. Read firmware from the server and write it to the flash of the root node
     */

    const esp_partition_t *updata_partition = esp_ota_get_next_update_partition(NULL);
    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(updata_partition, total_size, &ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_begin failed, total_size", esp_err_to_name(ret));

    for (ssize_t size = 0, recv_size = 0; recv_size < total_size; recv_size += size) {
        size = esp_http_client_read(client, (char *)data, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(size < 0, EXIT, "<%s> Read data from http stream", esp_err_to_name(ret));

        if (size > 0) {
            /**< Write OTA update data to partition */
            ret = esp_ota_write(ota_handle, data, OTA_DATA_PAYLOAD_LEN);
            ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> Write firmware to flash, size: %u, data: %.*s",
                           esp_err_to_name(ret), size, size, data);
        } else {
            ESP_LOGW(TAG, "<%s> esp_http_client_read", esp_err_to_name(ret));
            goto EXIT;
        }
    }

    ESP_LOGI(TAG, "The service download firmware is complete, Spend time: %" PRIu32 "s",
             (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000);

    ret = esp_ota_end(ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_end", esp_err_to_name(ret));

EXIT:
    ESP_FREE(data);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return total_size;
}

esp_err_t app_ota_initiator_data_cb(size_t src_offset, void *dst, size_t size)
{
    static const esp_partition_t *data_partition = NULL;

    if (!data_partition) {
        data_partition = esp_ota_get_next_update_partition(NULL);
    }

    return esp_partition_read(data_partition, src_offset, dst, size);
}

static void app_firmware_send(size_t firmware_size, uint8_t sha[ESPNOW_OTA_HASH_LEN])
{
    esp_err_t ret       = ESP_OK;
    uint32_t start_time = xTaskGetTickCount();
    espnow_ota_result_t espnow_ota_result = {0};
    espnow_ota_responder_t *info_list = NULL;
    espnow_addr_t *dest_addr_list = NULL;
    size_t num = 0;

    espnow_ota_initiator_scan(&info_list, &num, pdMS_TO_TICKS(3000));
    ESP_LOGW(TAG, "espnow wait ota num: %u", num);

    if (!num) {
        goto EXIT;
    }

    dest_addr_list = ESP_MALLOC(num * ESPNOW_ADDR_LEN);

    for (size_t i = 0; i < num; i++) {
        memcpy(dest_addr_list[i], info_list[i].mac, ESPNOW_ADDR_LEN);
    }

    espnow_ota_initiator_scan_result_free();

    ret = espnow_ota_initiator_send(dest_addr_list, num, sha, firmware_size, app_ota_initiator_data_cb, &espnow_ota_result);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> espnow_ota_initiator_send", esp_err_to_name(ret));

    if (espnow_ota_result.successed_num == 0) {
        ESP_LOGW(TAG, "Devices upgrade failed, unfinished_num: %u", espnow_ota_result.unfinished_num);
        goto EXIT;
    }

    ESP_LOGI(TAG, "Firmware is sent to the device to complete, Spend time: %" PRIu32 "s",
             (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000);
    ESP_LOGI(TAG, "Devices upgrade completed, successed_num: %u, unfinished_num: %u",
             espnow_ota_result.successed_num, espnow_ota_result.unfinished_num);

EXIT:
    ESP_FREE(dest_addr_list);
    espnow_ota_initiator_result_free(&espnow_ota_result);
}

#endif

static void app_wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void app_main()
{
    espnow_storage_init();

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

#ifdef CONFIG_APP_ESPNOW_OTA_INITIATOR
    uint8_t sha_256[32] = {0};
    const esp_partition_t *data_partition = esp_ota_get_next_update_partition(NULL);

    size_t firmware_size = app_firmware_download(CONFIG_APP_ESPNOW_FIRMWARE_UPGRADE_URL);
    esp_partition_get_sha256(data_partition, sha_256);

    app_firmware_send(firmware_size, sha_256);

#elif CONFIG_APP_ESPNOW_OTA_RESPONDER

    espnow_ota_config_t ota_config = {
        .skip_version_check       = true,
        .progress_report_interval = 10,
    };
    espnow_ota_responder_start(&ota_config);

#endif
}
