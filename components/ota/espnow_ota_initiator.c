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

#include "esp_utils.h"
#include "espnow_ota.h"
#include "espnow.h"

static const char *TAG = "espnow_ota_initatior";
static bool g_ota_send_running_flag   = false;
static SemaphoreHandle_t g_ota_send_exit_sem = NULL;

#ifndef CONFIG_ESPNOW_OTA_RETRY_COUNT
#define CONFIG_ESPNOW_OTA_RETRY_COUNT           50
#endif

#ifndef CONFIG_ESPNOW_OTA_SEND_RETRY_NUM
#define CONFIG_ESPNOW_OTA_SEND_RETRY_NUM        1
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

esp_err_t espnow_ota_initator_scan(espnow_ota_responder_t **info_list, size_t *num, TickType_t wait_ticks)
{
    esp_err_t ret = ESP_OK;
    uint8_t recv_addr[6] = {0};
    espnow_ota_info_t *recv_data = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t recv_size = 0;
    espnow_ota_info_t request_ota_info = {.type = ESPNOW_OTA_TYPE_REQUEST};
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast        = true,
        .magic            = esp_random(),
        .filter_adjacent_channel = true,
        .forward_ttl      = CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI,
    };

    *num       = 0;
    *info_list = NULL;

    espnow_set_qsize(ESPNOW_TYPE_OTA_STATUS, 32);

    for (int i = 0, start_ticks = xTaskGetTickCount(), recv_ticks = wait_ticks; i < 5 && wait_ticks - (xTaskGetTickCount() - start_ticks) > 0;
            ++i, recv_ticks = 500) {
        ret = espnow_send(ESPNOW_TYPE_OTA_DATA, ESPNOW_ADDR_BROADCAST, &request_ota_info, 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");

        for (; espnow_recv(ESPNOW_TYPE_OTA_STATUS, recv_addr, recv_data, &recv_size, &rx_ctrl, recv_ticks) == ESP_OK; recv_ticks = 100) {
            ESP_ERROR_CONTINUE(recv_data->type != ESPNOW_OTA_TYPE_INFO, MACSTR ", type: %d", MAC2STR(recv_addr), recv_data->type);

            bool info_list_is_exist = false;

            for (int i = 0; i < *num; ++i) {
                if (ESPNOW_ADDR_IS_EQUAL((*info_list)[i].mac, recv_addr)) {
                    info_list_is_exist = true;
                    break;
                }
            }

            if (info_list_is_exist) {
                continue;
            }

            *info_list = ESP_REALLOC(*info_list, (*num + 1) * sizeof(espnow_ota_responder_t));
            (*info_list)[*num].channel = rx_ctrl.channel;
            (*info_list)[*num].rssi    = rx_ctrl.rssi;
            memcpy((*info_list)[*num].mac, recv_addr, 6);
            memcpy(&((*info_list)[*num].app_desc), &recv_data->app_desc, sizeof(esp_app_desc_t));
            (*num)++;

            ESP_LOGV(TAG, "Application information:");
            ESP_LOGV(TAG, "Project name:     %s", recv_data->app_desc.project_name);
            ESP_LOGV(TAG, "App version:      %s", recv_data->app_desc.version);
            ESP_LOGV(TAG, "Secure version:   %d", recv_data->app_desc.secure_version);
            ESP_LOGV(TAG, "Compile time:     %s %s", recv_data->app_desc.date, recv_data->app_desc.time);
            ESP_LOGV(TAG, "ESP-IDF:          %s", recv_data->app_desc.idf_ver);
        }
    }

    return ESP_OK;
}

static esp_err_t espnow_ota_request_status(uint8_t (*progress_array)[ESPNOW_OTA_PROGRESS_MAX_SIZE],
        const espnow_ota_status_t *status, espnow_ota_result_t *result)
{
    esp_err_t ret       = ESP_OK;
    uint8_t src_addr[6] = {0};
    size_t data_size    = 0;
    espnow_ota_status_t *response_data = ESP_MALLOC(ESPNOW_DATA_LEN);

    result->requested_num = 0;
    ESP_FREE(result->requested_addr);

    /**
     * @brief Remove the device that the firmware upgrade has completed.
     */
    while (espnow_recv(ESPNOW_TYPE_OTA_STATUS, src_addr, response_data, &data_size, NULL, 0) == ESP_OK) {
        ESP_ERROR_CONTINUE(response_data->type != ESPNOW_OTA_TYPE_STATUS, MACSTR ", esponse_status.type: %d", MAC2STR(src_addr), response_data->type);

        if (response_data->written_size == response_data->total_size || response_data->error_code == ESP_ERR_ESPNOW_OTA_FINISH) {
            if (!addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr)) {
                ESP_LOGW(TAG, "The device has been removed from the list waiting for the upgrade");
                continue;
            }

            result->successed_num++;
            result->successed_addr = ESP_REALLOC_RETRY(result->successed_addr,
                                     result->successed_num * ESPNOW_ADDR_LEN);
            memcpy(result->successed_addr + (result->successed_num - 1), src_addr, ESPNOW_ADDR_LEN);
        } else if (response_data->error_code == ESP_ERR_ESPNOW_OTA_STOP) {
            addrs_remove(result->unfinished_addr, &result->unfinished_num, src_addr);
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
        .retransmit_count = 10,
        .magic    = esp_random(),
        .filter_adjacent_channel = true,
        .forward_ttl      = CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI,
    };

    for (int i = 0, wait_ticks = pdMS_TO_TICKS(500); i < 3 && response_num > 0; ++i, wait_ticks = pdMS_TO_TICKS(100)) {
        if (espnow_send(ESPNOW_TYPE_OTA_DATA, ESPNOW_ADDR_GROUP_OTA, status,
                        sizeof(espnow_ota_status_t), &status_frame, portMAX_DELAY) != ESP_OK) {
            ESP_LOGW(TAG, "Request devices upgrade status");
        }

        uint8_t mac_ota_wait[6] = {0};

        while (response_num > 0) {
            ret = espnow_recv(ESPNOW_TYPE_OTA_STATUS, src_addr, response_data, &data_size, NULL, wait_ticks);
            ESP_ERROR_BREAK(ret != ESP_OK, "<%s> wait_ticks: %d", esp_err_to_name(ret), wait_ticks);
            ESP_ERROR_CONTINUE(response_data->type != ESPNOW_OTA_TYPE_STATUS, "esponse_status.type: %d", response_data->type);
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

                espnow_send_group((uint8_t (*)[6])src_addr, 1, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
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

esp_err_t espnow_ota_initator_send(const uint8_t addrs_list[][6], size_t addrs_num,
                                   const uint8_t sha_256[ESPNOW_OTA_HASH_LEN], size_t size,
                                   espnow_ota_initator_data_cb_t ota_data_cb, espnow_ota_result_t *res)
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
        .retransmit_count = CONFIG_ESPNOW_OTA_SEND_RETRY_NUM,
        .group            = true,
        .forward_ttl      = CONFIG_ESPNOW_OTA_SEND_FORWARD_TTL,
        .forward_rssi     = CONFIG_ESPNOW_OTA_SEND_FORWARD_RSSI,
    };

    if (addrs_num == 1 && ESPNOW_ADDR_IS_BROADCAST(addrs_list[0])) {
        espnow_ota_responder_t *info_list = NULL;
        size_t info_num = 0;
        ret = espnow_ota_initator_scan(&info_list, &info_num, 3000);
        ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> espnow_ota_initator_scan", esp_err_to_name(ret));

        ESP_LOGI(TAG, "Scan OTA list, num: %d", info_num);

        result->unfinished_num  = info_num;
        result->unfinished_addr = ESP_MALLOC(result->unfinished_num * ESPNOW_ADDR_LEN);

        for (size_t i = 0; i < info_num; i++) {
            memcpy(result->unfinished_addr[i], info_list[i].mac, ESPNOW_ADDR_LEN);
        }

        ESP_FREE(info_list);
    } else {
        result->unfinished_num  = addrs_num;
        result->unfinished_addr = ESP_CALLOC(result->unfinished_num, ESPNOW_ADDR_LEN);
        memcpy(result->unfinished_addr, addrs_list, result->unfinished_num * ESPNOW_ADDR_LEN);
    }

    espnow_send_group(addrs_list, addrs_num, ESPNOW_ADDR_GROUP_OTA, NULL, true, portMAX_DELAY);
    espnow_set_qsize(ESPNOW_TYPE_OTA_STATUS, 32);

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

        ESP_LOGI(TAG, "count: %d, Upgrade_initator_send, requested_num: %d, unfinished_num: %d, successed_num: %d",
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
                ret = espnow_send(ESPNOW_TYPE_OTA_DATA, ESPNOW_ADDR_GROUP_OTA, packet, sizeof(espnow_ota_packet_t), &frame_head, portMAX_DELAY);
                ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow write", esp_err_to_name(ret));
            }
        }
    }

EXIT:

    // espnow_set_qsize(ESPNOW_TYPE_OTA_STATUS, 0);

    if (result->unfinished_num > 0) {
        espnow_send_group(result->unfinished_addr, result->unfinished_num, ESPNOW_ADDR_GROUP_OTA, NULL, false, portMAX_DELAY);
        ret = ESP_ERR_ESPNOW_OTA_FIRMWARE_INCOMPLETE;
    }

    g_ota_send_running_flag = false;

    if (res) {
        memcpy(res, result, sizeof(espnow_ota_result_t));
    } else {
        espnow_ota_initator_result_free(result);
    }

    ESP_FREE(packet);
    ESP_FREE(progress_array);
    ESP_FREE(result);

    if (g_ota_send_exit_sem) {
        xSemaphoreGive(g_ota_send_exit_sem);
    }

    return ret;
}

esp_err_t espnow_ota_initator_result_free(espnow_ota_result_t *result)
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

esp_err_t espnow_ota_initator_stop()
{
    if (!g_ota_send_running_flag) {
        return ESP_OK;
    }

    if (!g_ota_send_exit_sem) {
        g_ota_send_exit_sem = xSemaphoreCreateBinary();
    }

    g_ota_send_running_flag = false;

    xSemaphoreTake(g_ota_send_exit_sem, portMAX_DELAY);
    vQueueDelete(g_ota_send_exit_sem);
    g_ota_send_exit_sem = NULL;

    return ESP_OK;
}
