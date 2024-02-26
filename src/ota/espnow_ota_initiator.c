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

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "espnow.h"
#include "espnow_ota.h"
#include "espnow_utils.h"

static const char *TAG = "espnow_ota_initatior";
static bool g_ota_send_running_flag   = false;
static SemaphoreHandle_t g_ota_send_exit_sem = NULL;

static size_t g_scan_num = 0;
static espnow_ota_responder_t *g_info_list = NULL;
static bool g_info_en = false;

#ifndef CONFIG_ESPNOW_OTA_RETRY_COUNT
#define CONFIG_ESPNOW_OTA_RETRY_COUNT           50
#endif

#ifndef CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL
#define CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL      0
#endif

#ifndef CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI
#define CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI     -65
#endif

#ifndef CONFIG_ESPNOW_OTA_WAIT_RESPONSE_TIMEOUT
#define CONFIG_ESPNOW_OTA_WAIT_RESPONSE_TIMEOUT (10 * 1000)
#endif


static bool addrs_remove(uint8_t addrs_list[][ESPNOW_ADDR_LEN],
                         size_t *addrs_num, const uint8_t addr[6])
{
    if (!addrs_list || !addrs_num || !addr) {
        ESP_LOGE(TAG, "!addrs_list: %p !addrs_num: %p !addr: %p", addrs_list, addrs_num, addr);
        return false;
    }

    for (int i = 0; i < *addrs_num; i++) {
        if (ESPNOW_ADDR_IS_EQUAL(addrs_list[i], addr)) {
            if (--(*addrs_num)) {
                memcpy(addrs_list[i], addrs_list[*addrs_num], ESPNOW_ADDR_LEN);
            }

            return true;
        }
    }

    return false;
}

static esp_err_t espnow_ota_info(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    espnow_ota_info_t *recv_data = (espnow_ota_info_t *)data;

    for (int i = 0; i < g_scan_num; ++i) {
        if (ESPNOW_ADDR_IS_EQUAL((g_info_list)[i].mac, src_addr)) {
            return ESP_OK;
        }
    }

    g_info_list = ESP_REALLOC(g_info_list, (g_scan_num + 1) * sizeof(espnow_ota_responder_t));
    (g_info_list)[g_scan_num].channel = rx_ctrl->channel;
    (g_info_list)[g_scan_num].rssi    = rx_ctrl->rssi;
    memcpy((g_info_list)[g_scan_num].mac, src_addr, 6);
    memcpy(&((g_info_list)[g_scan_num].app_desc), &recv_data->app_desc, sizeof(esp_app_desc_t));
    g_scan_num++;

    ESP_LOGV(TAG, "Application information:");
    ESP_LOGV(TAG, "Project name:     %s", recv_data->app_desc.project_name);
    ESP_LOGV(TAG, "App version:      %s", recv_data->app_desc.version);
    ESP_LOGV(TAG, "Secure version:   %d", recv_data->app_desc.secure_version);
    ESP_LOGV(TAG, "Compile time:     %s %s", recv_data->app_desc.date, recv_data->app_desc.time);
    ESP_LOGV(TAG, "ESP-IDF:          %s", recv_data->app_desc.idf_ver);

    return ESP_OK;
}

static QueueHandle_t g_ota_queue = NULL;
typedef struct {
    uint8_t src_addr[6];
    void *data;
    size_t size;
} espnow_ota_data_t;

static esp_err_t espnow_ota_status_handle(uint8_t *src_addr, void *data, size_t size)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);

    if (g_ota_queue) {
        espnow_ota_data_t ota_data = { 0 };
        ota_data.data = ESP_MALLOC(size);
        if (!ota_data.data) {
            return ESP_FAIL;
        }
        memcpy(ota_data.data, data, size);
        ota_data.size = size;
        memcpy(ota_data.src_addr, src_addr, 6);
        if (xQueueSend(g_ota_queue, &ota_data, 0) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send ota queue failed", __func__, __LINE__);
            ESP_FREE(ota_data.data);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static esp_err_t espnow_ota_initiator_status_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    uint8_t data_type = ((uint8_t *)data)[0];

    switch (data_type) {
        case ESPNOW_OTA_TYPE_INFO:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_INFO");
            if (g_info_en){
                ret = espnow_ota_info(src_addr, data, size, rx_ctrl);
            }
            break;

        case ESPNOW_OTA_TYPE_STATUS:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_STATUS");
            ret = espnow_ota_status_handle(src_addr, data, size);
            break;

        default:
            break;
    }

    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_ota_handle");
    return ret;
}

esp_err_t espnow_ota_initiator_scan(espnow_ota_responder_t **info_list, size_t *num, TickType_t wait_ticks)
{
    esp_err_t ret = ESP_OK;
    espnow_ota_info_t request_ota_info = {.type = ESPNOW_OTA_TYPE_REQUEST};

    espnow_frame_head_t frame_head = {
        .retransmit_count = CONFIG_ESPNOW_OTA_RETRANSMISSION_TIMES,
        .broadcast        = true,
        .magic            = esp_random(),
        .filter_adjacent_channel = true,
        .forward_ttl      = CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI,
        .security         = CONFIG_ESPNOW_OTA_SECURITY,
    };

    espnow_ota_initiator_scan_result_free();

    g_info_en = true;
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_OTA_STATUS, 1, espnow_ota_initiator_status_process);

    for (int i = 0, start_ticks = xTaskGetTickCount(), recv_ticks = 500; i < 5 && wait_ticks - (xTaskGetTickCount() - start_ticks) > 0;
            ++i, recv_ticks = 500) {
        ret = espnow_send(ESPNOW_DATA_TYPE_OTA_DATA, ESPNOW_ADDR_BROADCAST, &request_ota_info, 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "espnow_send");

        vTaskDelay(recv_ticks);
    }

    *info_list = g_info_list;
    *num = g_scan_num;

EXIT:
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_OTA_STATUS, 0, NULL);
    g_info_en = false;

    return ret;
}

esp_err_t espnow_ota_initiator_scan_result_free(void)
{
    ESP_FREE(g_info_list);
    g_scan_num = 0;

    return ESP_OK;
}

static esp_err_t espnow_ota_request_status(uint8_t (*progress_array)[ESPNOW_OTA_PROGRESS_MAX_SIZE],
        const espnow_ota_status_t *status, espnow_ota_result_t *result)
{
    esp_err_t ret       = ESP_OK;
    uint8_t src_addr[6] = {0};
    espnow_ota_status_t *response_data = ESP_MALLOC(ESPNOW_DATA_LEN);
    espnow_ota_data_t ota_data = { 0 };

    result->requested_num = 0;
    ESP_FREE(result->requested_addr);

    /**
     * @brief Remove the device that the firmware upgrade has completed.
     */
    while (g_ota_queue && (xQueueReceive(g_ota_queue, &ota_data, 0) == pdPASS)) {
        memcpy(src_addr, ota_data.src_addr, 6);
        memcpy(response_data, ota_data.data, ota_data.size);
        ESP_FREE(ota_data.data);
        if (response_data->written_size == response_data->total_size || response_data->error_code == ESP_ERR_ESPNOW_OTA_FINISH) {
            if (!addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr)) {
                ESP_LOGW(TAG, "The device has been removed from the list waiting for the upgrade");
                continue;
            }

            result->successed_num++;
            result->successed_addr = ESP_REALLOC_RETRY(result->successed_addr,
                                     result->successed_num * ESPNOW_ADDR_LEN);
            memcpy(result->successed_addr + (result->successed_num - 1), src_addr, ESPNOW_ADDR_LEN);
            espnow_set_group((uint8_t (*)[6])src_addr, 1, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
        } else if (response_data->error_code == ESP_ERR_ESPNOW_OTA_STOP) {
            addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr);
            espnow_set_group((uint8_t (*)[6])src_addr, 1, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
        }

        if (result->unfinished_num == 0) {
            return ESP_OK;
        }
    }

    size_t response_num = result->unfinished_num;
    uint8_t (*response_addrs)[6] = ESP_REALLOC_RETRY(NULL, result->unfinished_num * ESPNOW_ADDR_LEN);
    memcpy(response_addrs, result->unfinished_addr, ESPNOW_ADDR_LEN * response_num);

    /**
     * @brief Request all devices upgrade status from unfinished device.
     */
    espnow_frame_head_t status_frame = {
        .group = true,
        .broadcast = true,
        .retransmit_count = CONFIG_ESPNOW_OTA_RETRANSMISSION_TIMES,
        .magic    = esp_random(),
        .filter_adjacent_channel = true,
        .forward_ttl      = CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI,
        .security         = CONFIG_ESPNOW_OTA_SECURITY,
    };

    for (int i = 0, wait_ticks = pdMS_TO_TICKS(1000); i < 3 && response_num > 0; ++i, wait_ticks = pdMS_TO_TICKS(500)) {
        if (espnow_send(ESPNOW_DATA_TYPE_OTA_DATA, ESPNOW_ADDR_GROUP_OTA, status,
                        sizeof(espnow_ota_status_t), &status_frame, portMAX_DELAY) != ESP_OK) {
            ESP_LOGW(TAG, "Request devices upgrade status");
        }

        uint8_t mac_ota_wait[6] = {0};

        while (response_num > 0 && g_ota_queue) {
            ret = xQueueReceive(g_ota_queue, &ota_data, wait_ticks);
            ESP_ERROR_BREAK(ret != pdPASS, "<%s> wait_ticks: %d", esp_err_to_name(ret), wait_ticks);
            memcpy(src_addr, ota_data.src_addr, 6);
            memcpy(response_data, ota_data.data, ota_data.size);
            ESP_FREE(ota_data.data);
            ret = response_data->error_code;

            if (response_data->error_code == ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT) {
                wait_ticks = pdMS_TO_TICKS(CONFIG_ESPNOW_OTA_WAIT_RESPONSE_TIMEOUT);
                memcpy(mac_ota_wait, src_addr, 6);
                continue;
            }

            if (response_data->error_code == ESP_ERR_ESPNOW_OTA_STOP
                    || response_data->error_code == ESP_ERR_ESPNOW_OTA_FINISH) {
                ESP_LOGW(TAG, "<ESP_ERR_ESPNOW_OTA_FIRMWARE_PARTITION> response_data->error_code: ");
                addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr);
                addrs_remove(response_addrs, &response_num, src_addr);
                espnow_set_group((uint8_t (*)[6])src_addr, 1, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
                continue;
            }

            ESP_LOGD(TAG, "Response, src_addr: " MACSTR ", response_num: %d, total_size: %d, written_size: %d, error_code: %s",
                     MAC2STR(src_addr), response_num, response_data->total_size,
                     response_data->written_size, esp_err_to_name(response_data->error_code));

            if (wait_ticks == pdMS_TO_TICKS(CONFIG_ESPNOW_OTA_WAIT_RESPONSE_TIMEOUT)) {
                if (!memcmp(src_addr, mac_ota_wait, 6)) {
                    wait_ticks = pdMS_TO_TICKS(100);
                }
            }

            /**< Remove the device that upgrade status has been received */
            if (!addrs_remove(response_addrs, &response_num, src_addr)) {
                continue;
            }

            if (response_data->written_size == response_data->total_size) {
                if (!addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr)) {
                    ESP_LOGW(TAG, "The device has been removed from the list waiting for the upgrade");
                    continue;
                }

                result->successed_num++;
                result->successed_addr = ESP_REALLOC_RETRY(result->successed_addr,
                                         result->successed_num * ESPNOW_ADDR_LEN);
                memcpy(result->successed_addr + (result->successed_num - 1), src_addr, ESPNOW_ADDR_LEN);

                espnow_set_group((uint8_t (*)[6])src_addr, 1, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
            } else {
                ESP_LOG_BUFFER_HEXDUMP(TAG, response_data->progress_array[0],
                                       sizeof(espnow_ota_status_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE, ESP_LOG_VERBOSE);

                result->requested_num++;
                result->requested_addr = ESP_REALLOC_RETRY(result->requested_addr, result->requested_num * ESPNOW_ADDR_LEN);
                memcpy(result->requested_addr + (result->requested_num - 1), src_addr, ESPNOW_ADDR_LEN);

                if (response_data->written_size == 0) {
                    memset(progress_array, 0x0, status->packet_num / 8 + 1);
                } else {
                    for (int i = 0; i < ESPNOW_OTA_PROGRESS_MAX_SIZE
                            && (response_data->progress_index * ESPNOW_OTA_PROGRESS_MAX_SIZE  + i) * 8 < status->packet_num; i++) {
                        progress_array[response_data->progress_index][i] &= response_data->progress_array[0][i];
                    }
                }
            }
        }
    }

    ret = ESP_OK;

    if (response_num && response_num == result->unfinished_num) {
        ESP_LOGW(TAG, "ESP_ERR_ESPNOW_OTA_DEVICE_NO_EXIST");
        ret = ESP_ERR_ESPNOW_OTA_DEVICE_NO_EXIST;
    } else if (response_num > 0) {
        ESP_LOGW(TAG, "ESP_ERR_ESPNOW_OTA_SEND_PACKET_LOSS");
        ret = ESP_ERR_ESPNOW_OTA_SEND_PACKET_LOSS;
    } else if (result->requested_num > 0) {
        ret = ESP_ERR_ESPNOW_OTA_FIRMWARE_INCOMPLETE;
        ESP_LOGD(TAG, "ESP_ERR_ESPNOW_OTA_FIRMWARE_INCOMPLETE");
    }

    ESP_FREE(response_addrs);
    ESP_FREE(response_data);
    return ret;
}

esp_err_t espnow_ota_initiator_send(const uint8_t addrs_list[][6], size_t addrs_num,
                                   const uint8_t sha_256[ESPNOW_OTA_HASH_LEN], size_t size,
                                   espnow_ota_initiator_data_cb_t ota_data_cb, espnow_ota_result_t *res)
{
    ESP_PARAM_CHECK(addrs_list);
    ESP_PARAM_CHECK(addrs_num);

    esp_err_t ret = ESP_OK;
    espnow_ota_status_t status = {
        .type = ESPNOW_OTA_TYPE_STATUS,
        .total_size = size,
        .packet_num = (size + ESPNOW_OTA_PACKET_MAX_SIZE - 1) / ESPNOW_OTA_PACKET_MAX_SIZE,
    };
    memcpy(status.sha_256, sha_256, ESPNOW_OTA_HASH_LEN);

    ESP_LOGI(TAG, "[%s, %d]: total_size: %d, packet_num: %d", __func__, __LINE__, size, status.packet_num);

    espnow_ota_packet_t *packet = ESP_MALLOC(sizeof(espnow_ota_packet_t));
    uint8_t (*progress_array)[ESPNOW_OTA_PROGRESS_MAX_SIZE] = ESP_MALLOC(status.packet_num / 8 + 1);
    espnow_ota_result_t *result = ESP_CALLOC(1, sizeof(espnow_ota_result_t));
    g_ota_send_running_flag = true;

    espnow_frame_head_t frame_head = {
        .broadcast        = true,
        .retransmit_count = CONFIG_ESPNOW_OTA_RETRANSMISSION_TIMES,
        .group            = true,
        .forward_ttl      = CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI,
        .security         = CONFIG_ESPNOW_OTA_SECURITY,
    };

    if (addrs_num == 1 && ESPNOW_ADDR_IS_BROADCAST(addrs_list[0])) {
        espnow_ota_responder_t *info_list = NULL;
        size_t info_num = 0;
        ret = espnow_ota_initiator_scan(&info_list, &info_num, 3000);
        ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> espnow_ota_initiator_scan", esp_err_to_name(ret));

        ESP_LOGI(TAG, "Scan OTA list, num: %d", info_num);

        result->unfinished_num  = info_num;
        result->unfinished_addr = ESP_MALLOC(result->unfinished_num * ESPNOW_ADDR_LEN);

        for (size_t i = 0; i < info_num; i++) {
            memcpy(result->unfinished_addr[i], info_list[i].mac, ESPNOW_ADDR_LEN);
        }

        espnow_ota_initiator_scan_result_free();
    } else {
        result->unfinished_num  = addrs_num;
        result->unfinished_addr = ESP_CALLOC(result->unfinished_num, ESPNOW_ADDR_LEN);
        memcpy(result->unfinished_addr, addrs_list, result->unfinished_num * ESPNOW_ADDR_LEN);
    }

    espnow_set_group(addrs_list, addrs_num, ESPNOW_ADDR_GROUP_OTA, NULL, true, portMAX_DELAY);
    /* Set queue size to unfinished num to avoid send queue failed */
    g_ota_queue = xQueueCreate(result->unfinished_num, sizeof(espnow_ota_data_t));
    ESP_ERROR_GOTO(!g_ota_queue, EXIT, "Create espnow ota queue fail");
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_OTA_STATUS, 1, espnow_ota_initiator_status_process);

    packet->type = ESPNOW_OTA_TYPE_DATA;
    packet->size = ESPNOW_OTA_PACKET_MAX_SIZE;

    ESP_LOGD(TAG, "packet_num: %d, total_size: %d", status.packet_num, status.total_size);

    for (int i = 0; i < CONFIG_ESPNOW_OTA_RETRY_COUNT && result->unfinished_num > 0 && g_ota_send_running_flag; ++i) {
        /**
         * @brief Request all devices upgrade status.
         */
        memset(progress_array, 0xff, status.packet_num / 8 + 1);

        ret = espnow_ota_request_status(progress_array, &status, result);
        ESP_ERROR_BREAK(ret == ESP_OK || ret == ESP_ERR_ESPNOW_OTA_DEVICE_NO_EXIST, "");

        ESP_LOGI(TAG, "count: %d, Upgrade_initiator_send, requested_num: %d, unfinished_num: %d, successed_num: %d",
                 i, result->unfinished_num, result->requested_num, result->successed_num);
        ESP_LOG_BUFFER_HEXDUMP(TAG, progress_array, sizeof(espnow_ota_status_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE, ESP_LOG_DEBUG);

        for (packet->seq = 0; result->requested_num > 0 && packet->seq < status.packet_num && g_ota_send_running_flag; ++packet->seq) {
            if (!ESPNOW_OTA_GET_BITS(progress_array, packet->seq)) {
                packet->size = (packet->seq == status.packet_num - 1) ? status.total_size - ESPNOW_OTA_PACKET_MAX_SIZE * packet->seq : ESPNOW_OTA_PACKET_MAX_SIZE;
                /**
                 * @brief Read firmware data from Flash to send to unfinished device.
                 */
                ret = ota_data_cb(packet->seq * ESPNOW_OTA_PACKET_MAX_SIZE, packet->data, packet->size);
                ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> Read data from Flash", esp_err_to_name(ret));

                ESP_LOGD(TAG, "seq: %d, size: %d, addrs_num: %d", packet->seq, packet->size, result->requested_num);
                ret = espnow_send(ESPNOW_DATA_TYPE_OTA_DATA, ESPNOW_ADDR_GROUP_OTA, packet, sizeof(espnow_ota_packet_t), &frame_head, portMAX_DELAY);
                ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow write", esp_err_to_name(ret));
            }
        }
    }

EXIT:

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_OTA_STATUS, 0, NULL);
    if (g_ota_queue) {
        espnow_ota_data_t *tmp_data = NULL;

        while (xQueueReceive(g_ota_queue, &tmp_data, 0)) {
            ESP_FREE(tmp_data->data);
        }

        vQueueDelete(g_ota_queue);
        g_ota_queue = NULL;
    }

    if (result->unfinished_num > 0) {
        espnow_set_group(result->unfinished_addr, result->unfinished_num, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
        ret = ESP_ERR_ESPNOW_OTA_FIRMWARE_INCOMPLETE;
    }

    g_ota_send_running_flag = false;

    if (res) {
        memcpy(res, result, sizeof(espnow_ota_result_t));
    } else {
        espnow_ota_initiator_result_free(result);
    }

    ESP_FREE(packet);
    ESP_FREE(progress_array);
    ESP_FREE(result);

    if (g_ota_send_exit_sem) {
        xSemaphoreGive(g_ota_send_exit_sem);
    }

    return ret;
}

esp_err_t espnow_ota_initiator_result_free(espnow_ota_result_t *result)
{
    ESP_PARAM_CHECK(result);

    result->unfinished_num = 0;
    result->requested_num  = 0;
    result->successed_num  = 0;
    ESP_FREE(result->unfinished_addr);
    ESP_FREE(result->requested_addr);
    ESP_FREE(result->successed_addr);

    return ESP_OK;
}

esp_err_t espnow_ota_initiator_stop()
{
    if (!g_ota_send_running_flag) {
        return ESP_OK;
    }

    if (!g_ota_send_exit_sem) {
        g_ota_send_exit_sem = xSemaphoreCreateBinary();
    }

    g_ota_send_running_flag = false;

    xSemaphoreTake(g_ota_send_exit_sem, portMAX_DELAY);
    vSemaphoreDelete(g_ota_send_exit_sem);
    g_ota_send_exit_sem = NULL;

    return ESP_OK;
}
