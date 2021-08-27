// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"

#include "esp_utils.h"
#include "espnow_ota.h"

#include "espnow.h"

#define ESPNOW_OTA_STORE_CONFIG_KEY "upugrad_config"
#define CONFIG_ESPNOW_OTA_SKIP_VERSION_CHECK
typedef struct {
    esp_ota_handle_t handle;      /**< OTA handle */
    const esp_partition_t *partition; /**< Pointer to partition structure obtained using
                                           esp_partition_find_first or esp_partition_get */
    uint32_t start_time;         /**< Start time of the upgrade */
    espnow_ota_status_t status;  /**< Upgrade status */
} ota_config_t;

static const char *TAG = "espnow_ota_responder";
static ota_config_t *g_ota_config = NULL;
static bool g_ota_finished_flag        = false;
static espnow_frame_head_t g_frame_config = { 0 };
static espnow_ota_config_t *g_espnow_ota_config = NULL;

static esp_err_t validate_image_header(const esp_partition_t *update)
{
    esp_app_desc_t new_app_info;
    esp_app_desc_t running_app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    if (esp_ota_get_partition_description(update, &new_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", new_app_info.version);
    }

    if (!g_espnow_ota_config->skip_version_check
            && !memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version))) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /**
     * Secure version check from firmware image header prevents subsequent download and flash write of
     * entire firmware image. However this is optional because it is also taken care in API
     * esp_https_ota_finish at the end of OTA update procedure.
     */
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();

    if (new_app_info.secure_version < hw_sec_version) {
        ESP_LOGW(TAG, "New firmware security version is less than eFuse programmed, %d < %d", new_app_info.secure_version, hw_sec_version);
        return ESP_FAIL;
    }

#endif

    return ESP_OK;
}

static esp_err_t espnow_ota_info(const uint8_t *src_addr)
{
    esp_err_t ret = ESP_OK;
    /**< Remove useless data, sha256 of elf file (32 Byte) +  reserv2 (20 Byte)*/
    size_t size = sizeof(espnow_ota_info_t) - 32 - 20;
    espnow_ota_info_t *info = ESP_MALLOC(sizeof(espnow_ota_info_t));

    info->type = ESPNOW_OTA_TYPE_INFO;
    memcpy(&info->app_desc, esp_ota_get_app_description(), sizeof(esp_app_desc_t));

    ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, src_addr, info, size, &g_frame_config, portMAX_DELAY);

    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    ESP_LOGD(TAG, "Application information:");
    ESP_LOGD(TAG, "Project name:     %s", info->app_desc.project_name);
    ESP_LOGD(TAG, "App version:      %s", info->app_desc.version);
    ESP_LOGD(TAG, "Secure version:   %d", info->app_desc.secure_version);
    ESP_LOGD(TAG, "Compile time:     %s %s", info->app_desc.date, info->app_desc.time);
    ESP_LOGD(TAG, "ESP-IDF:          %s", info->app_desc.idf_ver);

    ESP_FREE(info);

    return ESP_OK;
}

static esp_err_t espnow_ota_status_handle(const espnow_addr_t src_addr, const espnow_ota_status_t *status, size_t size)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(status);
    ESP_PARAM_CHECK(size);

    esp_err_t ret        = ESP_ERR_NO_MEM;
    size_t response_size = sizeof(espnow_ota_status_t);
    uint8_t running_sha_256[32] = {0};

    if (!g_ota_config) {
        size_t config_size = sizeof(ota_config_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE * 10;
        g_ota_config   = ESP_CALLOC(1, config_size);
        ESP_ERROR_GOTO(!g_ota_config, EXIT, "<ESP_ERR_NO_MEM> g_ota_config");

        esp_storage_get(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config, 0);

        g_ota_config->start_time = xTaskGetTickCount();
        g_ota_config->partition = esp_ota_get_next_update_partition(NULL);
    }

    g_ota_config->status.type = ESPNOW_OTA_TYPE_STATUS;
    g_ota_config->status.error_code = ESP_OK;

    ret = esp_partition_get_sha256(esp_ota_get_running_partition(), running_sha_256);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_partition_get_sha256");

    if (!memcmp(running_sha_256, status->sha_256, ESPNOW_OTA_HASH_LEN)) {
        ESP_LOGW(TAG, "The firmware to be upgraded is the same as the currently running firmware, so no upgrade");
        g_ota_config->status.error_code   = ESP_ERR_ESPNOW_OTA_FINISH;
        g_ota_config->status.written_size = 0;
        goto EXIT;
    }

    /**< If g_ota_config->status has been created and
         once again upgrade the same name bin, just return ESP_OK */
    if (!memcmp(g_ota_config->status.sha_256, status->sha_256, ESPNOW_OTA_HASH_LEN)
            && g_ota_config->status.total_size == status->total_size) {
        ret = ESP_OK;
        goto EXIT;
    }

    memset(g_ota_config, 0, sizeof(ota_config_t));
    memcpy(&g_ota_config->status, status, sizeof(espnow_ota_status_t));
    memset(g_ota_config->status.progress_array, 0, status->packet_num / 8 + 1);
    g_ota_config->status.written_size = 0;
    g_ota_config->status.error_code = ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT;

    ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                      sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    ESP_LOGI(TAG, "The device starts to upgrade");

    g_ota_finished_flag = false;
    /**< Get partition info of currently running app
    Return the next OTA app partition which should be written with a new firmware.*/
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update  = esp_ota_get_next_update_partition(NULL);

    ret = ESP_ERR_ESPNOW_OTA_FIRMWARE_PARTITION;
    ESP_ERROR_GOTO(!running || !update, EXIT,
                   "No partition is found or flash read operation failed");
    ESP_LOGD(TAG, "Running partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             running->label, running->type, running->subtype, running->address);
    ESP_LOGD(TAG, "Update partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             update->label, update->type, update->subtype, update->address);

    g_ota_config->partition  = update;
    g_ota_config->start_time = xTaskGetTickCount();

    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(update, g_ota_config->status.total_size, &g_ota_config->handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "esp_ota_begin failed");

    /**< Save upgrade infomation to flash. */
    ret = esp_storage_set(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config,
                          sizeof(ota_config_t) + g_ota_config->status.packet_num / 8 + 1);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "info_store_save, ret: %d", ret);

    /**< Send ESP_EVENT_ESPNOW_OTA_STARTED event to the event handler */
    esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_OTA_STARTED, NULL, 0, 0);

    g_ota_config->status.error_code = ESP_OK;

EXIT:

    /**< Update g_ota_config->status */
    if (g_ota_config->status.written_size
            && g_ota_config->status.written_size != g_ota_config->status.total_size) {
        espnow_ota_status_t *tmp_status = ESP_MALLOC(sizeof(espnow_ota_status_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE);
        memcpy(tmp_status, &g_ota_config->status, sizeof(espnow_ota_status_t));

        for (int seq = 0; seq < tmp_status->packet_num; ++seq) {
            if (!ESPNOW_OTA_GET_BITS(g_ota_config->status.progress_array, seq)) {
                tmp_status->progress_index = seq / (ESPNOW_OTA_PROGRESS_MAX_SIZE * 8);
                memcpy(tmp_status->progress_array[0], g_ota_config->status.progress_array[tmp_status->progress_index], ESPNOW_OTA_PROGRESS_MAX_SIZE);
                ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, src_addr, tmp_status,
                                  sizeof(espnow_ota_status_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE, NULL, portMAX_DELAY);
                ESP_LOG_BUFFER_HEXDUMP(TAG, tmp_status->progress_array, ESPNOW_OTA_PROGRESS_MAX_SIZE, ESP_LOG_DEBUG);
                ESP_ERROR_BREAK(ret != ESP_OK, "espnow_send");

                ESP_LOGD(TAG, "status, total_size: %d, written_size: %d, progress_index: %d",
                         tmp_status->total_size, tmp_status->written_size, tmp_status->progress_index);
                break;
            }
        }

        ESP_FREE(tmp_status);
        return ret;
    }

    if (ret != ESP_OK) {
        g_ota_config->status.error_code = ret;
    }

    ESP_LOGD(TAG, "Response upgrade status, written_size: %d, response_size: %d, addr: " MACSTR,
             g_ota_config->status.written_size, response_size, MAC2STR(src_addr));
    ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                      sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    return ESP_OK;
}

static esp_err_t espnow_ota_write(const espnow_addr_t src_addr, const espnow_ota_packet_t *packet, size_t size)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(packet);
    ESP_PARAM_CHECK(size);

    esp_err_t ret = ESP_OK;

    if (!g_ota_config) {
        g_ota_config  = ESP_CALLOC(1, sizeof(espnow_ota_status_t) + 10 * ESPNOW_OTA_PROGRESS_MAX_SIZE);
        ESP_ERROR_RETURN(!g_ota_config, ESP_ERR_NO_MEM, "<ESP_ERR_NO_MEM> g_ota_config");

        /**< Get upgrade infomation to flash. */
        ret = esp_storage_get(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config, 0);

        g_ota_config->start_time = xTaskGetTickCount();
        g_ota_config->partition = esp_ota_get_next_update_partition(NULL);

        if (ret != ESP_OK) {
            ESP_FREE(g_ota_config);
            ESP_LOGW(TAG, "Upgrade configuration is not initialized");
            return ESP_ERR_ESPNOW_OTA_NOT_INIT;
        }
    }

    if (g_ota_config->status.error_code == ESP_ERR_ESPNOW_OTA_STOP) {
        g_ota_config->status.type         = ESPNOW_OTA_TYPE_STATUS;
        g_ota_config->status.written_size = 0;
        memset(g_ota_config->status.progress_array, 0, g_ota_config->status.packet_num / 8 + 1);
        esp_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);

        ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                          sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

        return ESP_OK;
    }

    ESP_ERROR_RETURN(packet->seq * ESPNOW_OTA_PACKET_MAX_SIZE > g_ota_config->status.total_size,
                     ESP_ERR_INVALID_ARG, "packet->seq: %d", packet->seq);

    /**< Received a duplicate packet */
    if (ESPNOW_OTA_GET_BITS(g_ota_config->status.progress_array, packet->seq)) {
        ESP_LOGD(TAG, "Received a duplicate packet, packet_seq: %d", packet->seq);
        return ESP_OK;
    }

    /**< Write firmware data to the update partition */
    ret = esp_partition_write(g_ota_config->partition, packet->seq * ESPNOW_OTA_PACKET_MAX_SIZE,
                              packet->data, packet->size);
    ESP_ERROR_RETURN(ret != ESP_OK, ESP_ERR_ESPNOW_OTA_FIRMWARE_DOWNLOAD,
                     "esp_partition_write %s", esp_err_to_name(ret));

    ESPNOW_OTA_SET_BITS(g_ota_config->status.progress_array, packet->seq);
    g_ota_config->status.written_size += packet->size;

    /**< Save OTA status periodically, it can be used to
         resumable data transfers from breakpoint after system reset */
    if (g_espnow_ota_config->progress_report_interval) {
        static uint32_t s_next_written_percentage = 0;
        uint32_t written_percentage = g_ota_config->status.written_size * 100 / g_ota_config->status.total_size;

        if (!s_next_written_percentage) {
            s_next_written_percentage = g_espnow_ota_config->progress_report_interval;
        }

        ESP_LOGD(TAG, "packet_seq: %d, packet_size: %d, written_size: %d, progress: %03d%%, next_percentage: %03d%%",
                 packet->seq, packet->size, g_ota_config->status.written_size, written_percentage, s_next_written_percentage);

        if (written_percentage == s_next_written_percentage) {
            ESP_LOGD(TAG, "Save the data of upgrade status to flash");
            s_next_written_percentage += g_espnow_ota_config->progress_report_interval;

            esp_storage_set(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config,
                            sizeof(ota_config_t) + g_ota_config->status.packet_num / 8 + 1);

            /**< Send ESP_EVENT_ESPNOW_OTA_STATUS event to the event handler */
            esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_OTA_STATUS, &written_percentage, sizeof(uint32_t), 0);

            ESP_LOGD(TAG, "packet_seq: %d, packet_size: %d, written_size: %d, progress: %d%%",
                     packet->seq, packet->size, g_ota_config->status.written_size, written_percentage);
        } else if (written_percentage > s_next_written_percentage) {
            s_next_written_percentage = (written_percentage / g_espnow_ota_config->progress_report_interval + 1) * g_espnow_ota_config->progress_report_interval;
        }
    }

    if (g_ota_config->status.written_size == g_ota_config->status.total_size) {
        ESP_LOG_BUFFER_CHAR_LEVEL(TAG, g_ota_config->status.progress_array,
                                  ESPNOW_OTA_PROGRESS_MAX_SIZE, ESP_LOG_VERBOSE);
        ESP_LOGI(TAG, "Write total_size: %d, written_size: %d, spend time: %ds",
                 g_ota_config->status.total_size, g_ota_config->status.written_size,
                 (xTaskGetTickCount() - g_ota_config->start_time) * portTICK_RATE_MS / 1000);

        /**< If ESP32 was reset duration OTA, and after restart, the update_handle will be invalid,
             but it still can switch boot partition and reboot successful */
        esp_ota_end(g_ota_config->handle);
        esp_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);

        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

        ret = validate_image_header(update_partition);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "validate_image_header");

        ret = esp_ota_set_boot_partition(update_partition);

        if (ret != ESP_OK) {
            g_ota_config->status.written_size = 0;
            g_ota_config->status.error_code   = ESP_ERR_ESPNOW_OTA_STOP;
            ESP_LOGW(TAG, "<%s> esp_ota_set_boot_partition", esp_err_to_name(ret));
            return ret;
        }

        /**< Send ESP_EVENT_ESPNOW_OTA_FINISH event to the event handler */
        g_ota_finished_flag = true;
        g_ota_config->status.type = ESPNOW_OTA_TYPE_STATUS;

        /**< Response firmware upgrade status to root node. */
        ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                          sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
    
        esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_OTA_FINISH, NULL, 0, 0);
    }

    return ESP_OK;
}

esp_err_t espnow_ota_responder_get_status(espnow_ota_status_t *status)
{
    ESP_PARAM_CHECK(status);
    ESP_ERROR_RETURN(!g_ota_config, ESP_ERR_NOT_SUPPORTED, "Mupgrade firmware is not initialized");

    memcpy(status, &g_ota_config->status, sizeof(espnow_ota_status_t));

    return ESP_OK;
}

esp_err_t espnow_ota_responder_stop()
{
    esp_err_t ret = ESP_OK;

    if (!g_ota_config) {
        return ESP_OK;
    }

    if (g_ota_finished_flag) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        ret = esp_ota_set_boot_partition(running);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_ota_set_boot_partition");
    }

    g_ota_config->status.type         = ESPNOW_OTA_TYPE_STATUS;
    g_ota_config->status.error_code   = ESP_ERR_ESPNOW_OTA_STOP;
    g_ota_config->status.written_size = 0;
    memset(g_ota_config->status.progress_array, 0, g_ota_config->status.packet_num / 8 + 1);
    esp_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);

    ret = espnow_send(ESPNOW_TYPE_OTA_STATUS, ESPNOW_ADDR_BROADCAST,
                      &g_ota_config->status, sizeof(espnow_ota_status_t), NULL, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    return ESP_OK;
}

static void espnow_ota_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    uint8_t src_addr[6]  = { 0 };
    uint8_t *data   = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t size    = 0;

    espnow_set_qsize(ESPNOW_TYPE_OTA_DATA, 16);

    for (;;) {
        ret = espnow_recv(ESPNOW_TYPE_OTA_DATA, src_addr, data, &size, NULL, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK && ret != ESP_ERR_WIFI_TIMEOUT, "espnow_broadcast, ret: %x, err_name: %s", ret, esp_err_to_name(ret));

        uint8_t data_type = ((uint8_t *)data)[0];
        espnow_add_peer(src_addr, NULL);

        switch (data_type) {
        case ESPNOW_OTA_TYPE_REQUEST:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_INFO");
            ret = espnow_ota_info(src_addr);
            break;

        case ESPNOW_OTA_TYPE_STATUS:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_STATUS");
            ret = espnow_ota_status_handle(src_addr, (espnow_ota_status_t *)data, size);
            break;

        case ESPNOW_OTA_TYPE_DATA:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_DATA");
            ret = espnow_ota_write(src_addr, (espnow_ota_packet_t *)data, size);
            break;

        default:
            break;
        }

        espnow_del_peer(src_addr);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_ota_handle");
    }

    espnow_set_qsize(ESPNOW_TYPE_OTA_DATA, 0);

    ESP_FREE(data);
    vTaskDelete(NULL);
}

esp_err_t espnow_ota_responder_start(const espnow_ota_config_t *config)
{
    ESP_PARAM_CHECK(config);

    g_espnow_ota_config = ESP_MALLOC(sizeof(espnow_ota_config_t));
    memcpy(g_espnow_ota_config, config, sizeof(espnow_ota_config_t));

    xTaskCreate(espnow_ota_task, "espnow_ota", 3 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
    return ESP_OK;
}
