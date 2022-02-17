// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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
#include <sys/param.h>

#include "esp_wifi.h"
#include "esp_console.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_utils.h"
#include "esp_storage.h"
#include "espnow.h"

#include "espnow_log.h"
#include "espnow_log_flash.h"

#define LOG_FLASH_FILE_MAX_NUM      2                                /**< Create several files */
#define LOG_FLASH_FILE_MAX_SIZE     CONFIG_DEBUG_LOG_FILE_MAX_SIZE   /**< File storage size */
#define LOG_FLASH_STORE_KEY         "log_info"
#define LOG_FLASH_STORE_NAMESPACE   "log_info"

/**
 * @brief Create files configuration
 */
typedef struct {
    size_t size; /**< Files size */
    int offset;  /**< Set file pointer offset */
    uint32_t addr;
} flash_log_info_t;

static int g_log_index                   = 0;
static flash_log_info_t *g_log_info      = NULL;
static bool g_espnow_log_flash_init_flag = false;
static const esp_partition_t *g_log_part = NULL;
static const char *TAG                   = "espnow_log_flash";

static esp_err_t log_info_storage_init()
{
    esp_err_t err = nvs_flash_init_partition(CONFIG_DEBUG_LOG_PARTITION_LABEL_NVS);
    ESP_ERROR_RETURN(err != ESP_OK, err, "NVS Flash init failed");

    return ESP_OK;
}

static void *log_info_storage_get(const char *key)
{
    esp_err_t err = ESP_FAIL;
    nvs_handle handle = 0;
    void *value = NULL;
    size_t required_size = 0;

    if ((err = nvs_open_from_partition(CONFIG_DEBUG_LOG_PARTITION_LABEL_NVS, LOG_FLASH_STORE_NAMESPACE,
                                       NVS_READONLY, &handle)) != ESP_OK) {
        ESP_LOGW(TAG, "<%s> NVS open for %s %s %s failed",
                 esp_err_to_name(err), CONFIG_DEBUG_LOG_PARTITION_LABEL_NVS, LOG_FLASH_STORE_NAMESPACE, key);
        return NULL;
    }

    if ((err = nvs_get_blob(handle, key, NULL, &required_size)) == ESP_OK) {
        value = ESP_CALLOC(required_size + 1, 1); /* + 1 for NULL termination */
        nvs_get_blob(handle, key, value, &required_size);
    } else if ((err = nvs_get_str(handle, key, NULL, &required_size)) == ESP_OK) {
        value = ESP_CALLOC(required_size + 1, 1); /* + 1 for NULL termination */
        nvs_get_str(handle, key, value, &required_size);
    }

    nvs_close(handle);
    return value;
}

static esp_err_t log_info_storage_set(const char *key, void *data, size_t len)
{
    nvs_handle handle;
    esp_err_t err;

    if ((err = nvs_open_from_partition(CONFIG_DEBUG_LOG_PARTITION_LABEL_NVS, LOG_FLASH_STORE_NAMESPACE,
                                       NVS_READWRITE, &handle)) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return ESP_FAIL;
    }

    if ((err = nvs_set_blob(handle, key, data, len)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write key %s with error %d size %d", key, err, len);
        nvs_close(handle);
        return ESP_FAIL;
    }

    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t log_info_storage_erase(const char *key)
{
    nvs_handle handle;
    esp_err_t err;

    if ((err = nvs_open_from_partition(CONFIG_DEBUG_LOG_PARTITION_LABEL_NVS, LOG_FLASH_STORE_NAMESPACE,
                                       NVS_READWRITE, &handle)) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return ESP_FAIL;
    }

    /**
     * @brief If key is LOG_FLASH_STORE_NAMESPACE, erase all info in LOG_FLASH_STORE_NAMESPACE
     */
    if (!strcmp(key, LOG_FLASH_STORE_NAMESPACE)) {
        err = nvs_erase_all(handle);
    } else {
        err = nvs_erase_key(handle, key);
    }

    nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t espnow_log_flash_init()
{
    if (g_espnow_log_flash_init_flag) {
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;

    err = log_info_storage_init();
    ESP_ERROR_RETURN(err != ESP_OK, err, "log_info_storage_init");

    esp_partition_iterator_t part_itra = esp_partition_find(ESP_PARTITION_TYPE_DATA,
                                         ESP_PARTITION_SUBTYPE_ANY, CONFIG_DEBUG_LOG_PARTITION_LABEL_DATA);
    ESP_ERROR_RETURN(!part_itra, ESP_ERR_NOT_SUPPORTED, "partition no find, subtype: 0x%x, label: %s",
                     ESP_PARTITION_SUBTYPE_ANY, CONFIG_DEBUG_LOG_PARTITION_LABEL_DATA);

    g_log_part = esp_partition_get(part_itra);

    ESP_ERROR_RETURN(!g_log_part, ESP_ERR_NOT_SUPPORTED, "esp_partition_get");
    ESP_ERROR_RETURN(g_log_part->size < LOG_FLASH_FILE_MAX_SIZE, ESP_ERR_NOT_SUPPORTED,
                     "Log file (%d Byte) size must be smaller than partition size (%d Byte).",
                     LOG_FLASH_FILE_MAX_SIZE, g_log_part->size);
    ESP_ERROR_RETURN(LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM % 4096 != 0, ESP_ERR_NOT_SUPPORTED,
                     "The size of the log partition must be an integer of %d KB.", LOG_FLASH_FILE_MAX_NUM * 4);

    g_log_info = log_info_storage_get(LOG_FLASH_STORE_KEY);

    if (!g_log_info) {
        g_log_info = ESP_CALLOC(LOG_FLASH_FILE_MAX_NUM, sizeof(flash_log_info_t));
        err = esp_partition_erase_range(g_log_part, CONFIG_DEBUG_LOG_PARTITION_OFFSET, LOG_FLASH_FILE_MAX_SIZE);
        ESP_ERROR_RETURN(err != ESP_OK, err, "esp_partition_erase_range");
    }

    /**< Create two files */
    for (size_t i = 0, min_size = LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM; i < LOG_FLASH_FILE_MAX_NUM; i++) {
        (g_log_info + i)->addr = CONFIG_DEBUG_LOG_PARTITION_OFFSET + LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM * i;

        /**< Whether file is full */
        if ((g_log_info + i)->size < LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM && (g_log_info + i)->size < min_size) {
            min_size = (g_log_info + i)->size;
            g_log_index  = i;  /**< File index */
        }
    }

    g_espnow_log_flash_init_flag = true;
    ESP_LOGI(TAG, "LOG flash initialized successfully");
    ESP_LOGI(TAG, "Log save partition subtype: label: %s, addr:0x%x, offset: %d, size: %d",
             CONFIG_DEBUG_LOG_PARTITION_LABEL_DATA, g_log_part->address, CONFIG_DEBUG_LOG_PARTITION_OFFSET, g_log_part->size);

    return ESP_OK;
}

esp_err_t espnow_log_flash_deinit()
{
    if (!g_espnow_log_flash_init_flag) {
        return ESP_FAIL;
    }

    g_espnow_log_flash_init_flag = false;

    ESP_LOGD(TAG, "Log flash de-initialized successfully");

    return ESP_OK;
}

esp_err_t espnow_log_flash_write(const char *data, size_t size, esp_log_level_t level)
{
    esp_err_t err              = ESP_OK;
    static bool flag_timestamp = true;

    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size > 0);

    if (!g_espnow_log_flash_init_flag) {
        return ESP_FAIL;
    }

    /**
     * @brief Get the address of the pointer to the next file and clear the file to be written
     * and clear offset size.
     */
    if ((g_log_info + g_log_index)->size + size > LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM) {
        g_log_index = (g_log_index + 1) % LOG_FLASH_FILE_MAX_NUM;
        (g_log_info + g_log_index)->size   = 0;
        (g_log_info + g_log_index)->offset = 0;

        err = esp_partition_erase_range(g_log_part, (g_log_info + g_log_index)->addr, LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM);
        ESP_ERROR_RETURN(err != ESP_OK, err, "esp_partition_erase_range, addr: %x", (g_log_info + g_log_index)->addr);
    }

    static uint32_t s_event_send_tick = 0;

    if (espnow_log_flash_size() > LOG_FLASH_FILE_MAX_SIZE / LOG_FLASH_FILE_MAX_NUM
            && (xTaskGetTickCount() - s_event_send_tick > 30000 || !s_event_send_tick)) {
        s_event_send_tick = xTaskGetTickCount();
        esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_LOG_FLASH_FULL, NULL, 0, portMAX_DELAY);
    }

    DEBUG_LOG_PRINTF("esp_partition_write, addr: %d, offset: %d, log_index: %d, size: %d\n",
                     (g_log_info + g_log_index)->addr, (g_log_info + g_log_index)->size, g_log_index, size);

    if (flag_timestamp) {
        flag_timestamp = false;

        /**< Get the current timestamp */
        time_t now = 0;
        struct tm log_time = {0};
        char strtime_buf[32] = {0};

        time(&now);
        localtime_r(&now, &log_time);
        strftime(strtime_buf, sizeof(strtime_buf), "[%Y-%m-%d %H:%M:%S] ", &log_time);

        /**
         * @brief Change the write file address, then write timestamp data
         */
        err = esp_partition_write(g_log_part, (g_log_info + g_log_index)->addr + (g_log_info + g_log_index)->size, strtime_buf, strlen(strtime_buf));
        ESP_ERROR_RETURN(err != ESP_OK, err, "esp_partition_write");
        (g_log_info + g_log_index)->size += strlen(strtime_buf);
    }

    /**
     * @brief Change the write file address, then write log data after the timestamp.
     *
     * @note First need to get the length of the timestamp, then write the log data after the timestamp
     */
    err = esp_partition_write(g_log_part, (g_log_info + g_log_index)->addr + (g_log_info + g_log_index)->size, data, size);
    ESP_ERROR_RETURN(err != ESP_OK, err, "esp_partition_write");
    (g_log_info + g_log_index)->size += size;

    /**
     * @brief Here in order to read the data address written by the file,Get where the data address is written.
     *
     */
    log_info_storage_set(LOG_FLASH_STORE_KEY, g_log_info, sizeof(flash_log_info_t) * LOG_FLASH_FILE_MAX_NUM);

    if (data[size - 1] == '\n') {
        flag_timestamp = true;
    }

    return ESP_OK;
}

esp_err_t espnow_log_flash_read(char *data, size_t *size)
{
    esp_err_t err    = ESP_OK;
    size_t read_size = 0;
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size && *size > 0);

    if (!g_espnow_log_flash_init_flag) {
        *size = 0;
        return ESP_FAIL;
    }

    /**
     * @brief First find Target file index address, then read the this files data
     *
     * @note The file address read is different from the file address written, so that the same address
     * can be prevented from being written and read.
     */
    for (int i = 0; i < LOG_FLASH_FILE_MAX_NUM && *size > read_size; ++i, data += read_size) {
        flash_log_info_t *log_info = g_log_info + (g_log_index + 1 + i) % LOG_FLASH_FILE_MAX_NUM;
        ssize_t data_size = MIN(*size - read_size, log_info->size - log_info->offset);

        if (data_size > 0) {
            err = esp_partition_read(g_log_part, log_info->addr + log_info->offset, data, data_size);

            if (err != ESP_OK) {
                ESP_LOGW(TAG, "<%s> fread packet, ret: %d, *size: %d, read_size: %d, log_info->offset: %d, log_info->size: %d, index: %d",
                         strerror(err), err, *size, read_size, log_info->offset, log_info->size, i);
                log_info->offset = log_info->size = 0;
                break;
            }

            read_size += data_size;
            log_info->offset += data_size;
            DEBUG_LOG_PRINTF("esp_partition_read, data_size: %d, read_size: %d, log_info->offset: %d, log_info->size: %d, ret: %d, index: %d, data: %.*s\n",
                             data_size, read_size, log_info->offset, log_info->size, err, i, data_size, data);

            log_info_storage_set(LOG_FLASH_STORE_KEY, g_log_info, sizeof(flash_log_info_t) * LOG_FLASH_FILE_MAX_NUM);
        }
    }

    *size = read_size;
    return read_size > 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t espnow_log_flash_erase()
{
    if (!g_espnow_log_flash_init_flag) {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;

    err = log_info_storage_erase(LOG_FLASH_STORE_KEY);
    ESP_ERROR_RETURN(err != ESP_OK, err, "log_info_storage_erase");

    err = esp_partition_erase_range(g_log_part, CONFIG_DEBUG_LOG_PARTITION_OFFSET, LOG_FLASH_FILE_MAX_SIZE);
    ESP_ERROR_RETURN(err != ESP_OK, err, "esp_partition_erase_range");

    return ESP_OK;
}

size_t espnow_log_flash_size()
{
    if (!g_espnow_log_flash_init_flag) {
        return 0;
    }

    size_t size = 0;

    for (size_t i = 0; i < LOG_FLASH_FILE_MAX_NUM; i++) {
        if ((g_log_info + i)->size >= (g_log_info + i)->offset) {
            size += (g_log_info + i)->size - (g_log_info + i)->offset;
        }
    }

    return size;
}
