/* OTA commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "esp_wifi.h"
#include "esp_utils.h"

#include "espnow.h"
#include "sdcard.h"
#include "debug_cmd.h"

#include "espnow_console.h"
#include "espnow_log.h"

#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_ctrl.h"

#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <errno.h>
#include <sys/param.h>

static struct {
    struct arg_str *download;
    struct arg_int *find;
    struct arg_str *send;
    struct arg_end *end;
} ota_args;

static const char *TAG = "ota_cmd";
static const esp_partition_t *g_ota_data_partition = NULL;
static size_t g_ota_size = 0;

static esp_err_t ota_initator_data_cb(size_t src_offset, void* dst, size_t size)
{
    return esp_partition_read(g_ota_data_partition, src_offset, dst, size);
}

static esp_err_t firmware_download(const char *url)
{
#define OTA_DATA_PAYLOAD_LEN 1460

    esp_err_t ret       = ESP_OK;
    uint8_t *data       = ESP_MALLOC(OTA_DATA_PAYLOAD_LEN);
    char name[32]       = {0x0};
    size_t total_size   = 0;
    int start_time      = 0;
    esp_ota_handle_t ota_handle = 0;

    /**
     * @note If you need to upgrade all devices, pass MWIFI_ADDR_ANY;
     *       If you upgrade the incoming address list to the specified device
     */

    esp_http_client_config_t config = {
        .url            = url,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
        .timeout_ms     = 10 * 1000,
    };

    /**
     * @brief 1. Connect to the server
     */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_ERROR_GOTO(!client, EXIT, "Initialise HTTP connection");

    start_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Open HTTP connection: %s", url);

    /**
     * @brief First, the firmware is obtained from the http server and stored on the root node.
     */
    do {
        ret = esp_http_client_open(client, 0);

        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "<%s> Connection service failed", esp_err_to_name(ret));
        }
    } while (ret != ESP_OK);

    total_size = esp_http_client_fetch_headers(client);
    sscanf(url, "%*[^/]//%*[^/]/%[^.bin]", name);

    if (total_size <= 0) {
        ESP_LOGW(TAG, "Please check the address of the server");
        ret = esp_http_client_read(client, (char *)data, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(ret < 0, EXIT, "<%s> Read data from http stream", esp_err_to_name(ret));

        ESP_LOGW(TAG, "Recv data: %.*s", ret, data);
        goto EXIT;
    }

    /**< Get partition info of currently running app
    Return the next OTA app partition which should be written with a new firmware.*/
    const esp_partition_t *running = esp_ota_get_running_partition();
    g_ota_data_partition  = esp_ota_get_next_update_partition(NULL);

    ESP_ERROR_RETURN(!running || !g_ota_data_partition, ESP_ERR_ESPNOW_OTA_FIRMWARE_PARTITION,
                     "No partition is found or flash read operation failed");

    ESP_LOGI(TAG, "Running partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             running->label, running->type, running->subtype, running->address);
    ESP_LOGI(TAG, "Update partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             g_ota_data_partition->label, g_ota_data_partition->type, g_ota_data_partition->subtype, g_ota_data_partition->address);

    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(g_ota_data_partition, total_size, &ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_begin failed, total_size", esp_err_to_name(ret));

    /**
     * @brief 3. Read firmware from the server and write it to the flash of the root node
     */
    for (ssize_t size = 0, recv_size = 0, i = 0; recv_size < total_size; recv_size += size, ++i) {
        size = esp_http_client_read(client, (char *)data, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(size < 0, EXIT, "<%s> Read data from http stream", esp_err_to_name(ret));

        if (size > 0) {
            /**< Write OTA update data to partition */
            ret = esp_ota_write(ota_handle, data, size);
            ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> Write firmware to flash, size: %d, data: %.*s",
                esp_err_to_name(ret), size, size, data);

        } else {
            ESP_LOGW(TAG, "<%s> esp_http_client_read", esp_err_to_name(ret));
            goto EXIT;
        }

        if (i % 100 == 0 || recv_size == total_size) {
            ESP_LOGI(TAG, "Firmware download size: %d, progress rate: %d%%",
                     recv_size, recv_size * 100 / total_size);
        }
    }

    ESP_LOGI(TAG, "The service download firmware is complete, total_size: %d Spend time: %ds",
             total_size, (xTaskGetTickCount() - start_time) * portTICK_RATE_MS / 1000);
    
    g_ota_size = total_size;
    esp_storage_set("binary_len", &total_size, sizeof(size_t));

    ret = esp_ota_end(ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_end", esp_err_to_name(ret));

EXIT:
    ESP_FREE(data);
    return ESP_OK;
}

static void ota_send_task(void *arg)
{
    size_t addrs_num = 0;
    espnow_ota_result_t ota_result = {0};
    uint32_t start_time = xTaskGetTickCount();
    uint8_t (* addrs_list)[ESPNOW_ADDR_LEN] = ESP_MALLOC(ESPNOW_ADDR_LEN);

    for (const char *tmp = (char *)arg;; tmp++) {
        if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
            mac_str2hex(tmp - 17, addrs_list[addrs_num]);
            addrs_num++;

            if (*tmp == '\0' || *(tmp + 1) == '\0') {
                break;
            }

            addrs_list = ESP_REALLOC(addrs_list, ESPNOW_ADDR_LEN * (addrs_num + 1));
        }
    }

    uint8_t sha_256[32] = {0};
    esp_partition_get_sha256(g_ota_data_partition, sha_256);
    ESP_LOG_BUFFER_HEXDUMP(TAG, sha_256, 32, ESP_LOG_DEBUG);
    espnow_ota_initator_send(addrs_list, addrs_num, sha_256, g_ota_size,
                                    ota_initator_data_cb, &ota_result);

    ESP_LOGI(TAG, "Firmware is sent to the device to complete, Spend time: %ds",
                (xTaskGetTickCount() - start_time) * portTICK_RATE_MS / 1000);
    ESP_LOGI(TAG, "Devices upgrade completed, successed_num: %d, unfinished_num: %d",
                ota_result.successed_num, ota_result.unfinished_num);

    ESP_FREE(addrs_list);
    espnow_ota_initator_result_free(&ota_result);
    ESP_FREE(arg);

    vTaskDelete(NULL);
}

/**
 * @brief  A function which implements `ota` ota.
 */
static int ota_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &ota_args) != ESP_OK) {
        arg_print_errors(stderr, ota_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    if (ota_args.download->count) {
        ESP_LOGI(TAG, "Firmware Download, url: %s", ota_args.download->sval[0]);
        ret = firmware_download(ota_args.download->sval[0]);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> firmware_download", esp_err_to_name(ret));
    }

    if (ota_args.find->count) {
        ESP_LOGI(TAG, "Find upgradeable devices");

        size_t num = 0;
        espnow_ota_responder_t *info_list = NULL;
        ret = espnow_ota_initator_scan(&info_list, &num, pdMS_TO_TICKS(ota_args.find->ival[0]));
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_ota_initator_scan", esp_err_to_name(ret));

        if (num > 0) {
            char (*addrs_list)[18] = ESP_MALLOC(num * 18 + 1);

            for (int i = 0; i < num; ++i) {
                sprintf(addrs_list[i], MACSTR "|", MAC2STR(info_list[i].mac));
            }

            ESP_LOGI(TAG, "info, num: %d, list: %s", num, (char *)addrs_list);
            ESP_FREE(addrs_list);

            ESP_LOGI(TAG, "|         mac       | Channel | Rssi | Project name | ESP-IDF version | App version | Secure version | Compile time |");

            for (int i = 0; i < num; ++i) {
                ESP_LOGI(TAG, "| "MACSTR" |   %d   |  %d  | %12s | %15s | %11s | %d | %6s %6s |",
                         MAC2STR(info_list[i].mac), info_list[i].channel, info_list[i].rssi,
                         info_list[i].app_desc.project_name, info_list[i].app_desc.idf_ver, info_list[i].app_desc.version,
                         info_list[i].app_desc.secure_version, info_list[i].app_desc.date, info_list[i].app_desc.time);
            }
        }

        ESP_FREE(info_list);
    }

    if (ota_args.send->count) {
        ESP_LOGI(TAG, "Send firmware to selected device: %s", ota_args.send->sval[0]);

        if(!g_ota_data_partition && esp_storage_get("binary_len", &g_ota_size, sizeof(size_t)) != ESP_OK){
            ESP_LOGE(TAG, "Firmware not downloaded");
            return ESP_FAIL;
        }

        if(!g_ota_data_partition) {
            g_ota_data_partition  = esp_ota_get_next_update_partition(NULL);
        };

        char *addrs_list = ESP_CALLOC(1, strlen(ota_args.send->sval[0]) + 1);
        strcpy(addrs_list, ota_args.send->sval[0]);

        xTaskCreate(ota_send_task, "ota_send", 8 * 1024, addrs_list, tskIDLE_PRIORITY + 1, NULL);
    }

    return ESP_OK;
}

void register_ota()
{
    ota_args.download = arg_str0("d", "downloae", "<url>", "Firmware Download");
    ota_args.find     = arg_int0("f", "find", "<wait_tick>", "Find upgradeable devices");
    ota_args.send     = arg_str0("s", "send", "<xx:xx:xx:xx:xx:xx>,<xx:xx:xx:xx:xx:xx>", "Send firmware to selected device");
    ota_args.end      = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "ota",
        .help = "Firmware update",
        .hint = NULL,
        .func = &ota_func,
        .argtable = &ota_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
