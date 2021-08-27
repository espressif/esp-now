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

#pragma once

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"

#include "esp_utils.h"
#include "espnow.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#define ESPNOW_OTA_HASH_LEN     16
#define ESP_ERR_ESPNOW_OTA_BASE 0x1000
/**
 * @brief mupgrade error code definition
 */
#define ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT   (ESP_ERR_ESPNOW_OTA_BASE + 1) /**< Uninitialized firmware configuration */
#define ESP_ERR_ESPNOW_OTA_FIRMWARE_PARTITION  (ESP_ERR_ESPNOW_OTA_BASE + 2) /**< Partition table error */
#define ESP_ERR_ESPNOW_OTA_FIRMWARE_INVALID    (ESP_ERR_ESPNOW_OTA_BASE + 3) /**< Non-project generated firmware */
#define ESP_ERR_ESPNOW_OTA_FIRMWARE_INCOMPLETE (ESP_ERR_ESPNOW_OTA_BASE + 4) /**< The firmware received by the device is incomplete */
#define ESP_ERR_ESPNOW_OTA_FIRMWARE_DOWNLOAD   (ESP_ERR_ESPNOW_OTA_BASE + 5) /**< Firmware write flash error */
#define ESP_ERR_ESPNOW_OTA_FIRMWARE_FINISH     (ESP_ERR_ESPNOW_OTA_BASE + 6) /**< The firmware has been written to completion */
#define ESP_ERR_ESPNOW_OTA_DEVICE_NO_EXIST     (ESP_ERR_ESPNOW_OTA_BASE + 7) /**< The device that needs to be upgraded does not exist */
#define ESP_ERR_ESPNOW_OTA_SEND_PACKET_LOSS    (ESP_ERR_ESPNOW_OTA_BASE + 8) /**< Request device upgrade status failed */
#define ESP_ERR_ESPNOW_OTA_NOT_INIT            (ESP_ERR_ESPNOW_OTA_BASE + 9) /**< Upgrade configuration is not initialized */
#define ESP_ERR_ESPNOW_OTA_STOP                (ESP_ERR_ESPNOW_OTA_BASE + 10) /**< Upgrade configuration is not initialized */
#define ESP_ERR_ESPNOW_OTA_FINISH              (ESP_ERR_ESPNOW_OTA_BASE + 11) /**< Upgrade configuration is not initialized */

/**
 * @brief enumerated list of upgrade event id
 */
#define ESP_EVENT_ESPNOW_OTA_STARTED           (ESP_EVENT_ESPNOW_OTA_BASE + 1) /**< The device starts to upgrade */
#define ESP_EVENT_ESPNOW_OTA_STATUS            (ESP_EVENT_ESPNOW_OTA_BASE + 2) /**< Proactively report progress */
#define ESP_EVENT_ESPNOW_OTA_FINISH            (ESP_EVENT_ESPNOW_OTA_BASE + 3) /**< The upgrade is complete and the new firmware will run after the reboot */
#define ESP_EVENT_ESPNOW_OTA_STOPED            (ESP_EVENT_ESPNOW_OTA_BASE + 4) /**< Stop upgrading */
#define ESP_EVENT_ESPNOW_OTA_FIRMWARE_DOWNLOAD (ESP_EVENT_ESPNOW_OTA_BASE + 5) /**< Start firmware write flash  */
#define ESP_EVENT_ESPNOW_OTA_SEND_FINISH       (ESP_EVENT_ESPNOW_OTA_BASE + 6) /**< Send the firmware to other devices to complete */

/**
 * @brief Firmware subcontract upgrade
 */
#define ESPNOW_OTA_PROGRESS_MAX_SIZE           (200)  /**< Maximum length of a single packet transmitted */
#define ESPNOW_OTA_PACKET_MAX_SIZE             (226)  /**< Maximum length of a single packet transmitted */
#define ESPNOW_OTA_PACKET_MAX_NUM              (4 * 1024 * 1024/ ESPNOW_OTA_PACKET_MAX_SIZE) /**< The maximum number of packets */

/**
 * @brief Bit operations to get and modify a bit in an array
 */
#define ESPNOW_OTA_GET_BITS(data, bits)        ( (((uint8_t *)(data))[(bits) >> 0x3]) & ( 1 << ((bits) & 0x7)) )
#define ESPNOW_OTA_SET_BITS(data, bits)        do { (((uint8_t *)(data))[(bits) >> 0x3]) |= ( 1 << ((bits) & 0x7)); } while(0);

typedef struct {
    uint8_t type;
    esp_app_desc_t app_desc;
} espnow_ota_info_t;

typedef struct {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t channel;
    esp_app_desc_t app_desc;
} espnow_ota_responder_t;

/**
 * @brief Type of packet
 */
typedef enum {
    ESPNOW_OTA_TYPE_REQUEST,
    ESPNOW_OTA_TYPE_INFO,
    ESPNOW_OTA_TYPE_DATA,
    ESPNOW_OTA_TYPE_STATUS,
} espnow_ota_type_t;

/**
 * @brief Firmware packet
 */
typedef struct {
    uint8_t type;  /**< Type of packet, ESPNOW_OTA_TYPE_DATA */
    uint16_t seq;  /**< Sequence */
    uint8_t size;  /**< Size */
    uint8_t data[ESPNOW_OTA_PACKET_MAX_SIZE]; /**< Firmware */
} __attribute__((packed)) espnow_ota_packet_t;

typedef struct {
    bool skip_version_check;
    uint8_t progress_report_interval;
} espnow_ota_config_t;

/**
 * @brief Status packet
 */
typedef struct {
    uint8_t type;           /**< Type of packet, ESPNOW_OTA_TYPE_STATUS */
    uint8_t sha_256[ESPNOW_OTA_HASH_LEN];          /**< Unique identifier of the firmware */
    int16_t error_code;     /**< Upgrade status */
    uint16_t packet_num;    /**< Identify if each packet of data has been written */
    uint32_t total_size;    /**< Total length of firmware */
    uint32_t written_size;  /**< The length of the flash has been written */
    uint8_t progress_index; /**< Identify if each packet of data has been written */
    uint8_t progress_array[0][ESPNOW_OTA_PROGRESS_MAX_SIZE]; /**< Identify if each packet of data has been written */
} __attribute__((packed)) espnow_ota_status_t;

/**
 * @brief List of devices' status during the upgrade process
 */
typedef struct {
    size_t unfinished_num;    /**< The number of devices to be upgraded */
    espnow_addr_t *unfinished_addr; /**< MAC address of devices to be upgraded */

    size_t successed_num;     /**< The number of devices that succeeded to upgrade */
    espnow_addr_t *successed_addr;  /**< MAC address of devices that succeeded to upgrade */

    size_t requested_num;     /**< The number of devices to be upgraded */
    espnow_addr_t *requested_addr;  /**< This address is used to buffer the result of the request during the upgrade process */
} espnow_ota_result_t;

typedef esp_err_t (* espnow_ota_initator_data_cb_t)(size_t src_offset, void *dst, size_t size);

/**
 * @brief  Root sends firmware to other nodes
 *
 * @attention Only called at the root
 *
 * @param  dest_addrs     Destination nodes of mac
 * @param  dest_addrs_num Number of destination nodes
 * @param  result         Must call espnow_ota_initator_result_free to free memory
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT
 *    - ESP_ERR_ESPNOW_OTA_DEVICE_NO_EXIST
 */
esp_err_t espnow_ota_initator_send(const espnow_addr_t *addrs_list, size_t addrs_num,
                                   const uint8_t sha_256[ESPNOW_OTA_HASH_LEN], size_t size,
                                   espnow_ota_initator_data_cb_t ota_data_cb, espnow_ota_result_t *res);

/**
 * @brief Stop Root to send firmware to other nodes
 *
 * @return
 *    - ESP_OK
 *   - ESP_ERR_NOT_SUPPORTED
 */
esp_err_t espnow_ota_initator_stop();

/**
 * @brief  Free memory in the results list
 *
 * @param  result Pointer to device upgrade status
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_ota_initator_result_free(espnow_ota_result_t *result);

/**
 * @brief  Free memory in the results list
 *
 * @param  result Pointer to device upgrade status
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_ota_initator_scan(espnow_ota_responder_t **info_list, size_t *num, TickType_t wait_ticks);

/**
 * @brief Get the status of the upgrade
 *
 * @param status The status of the upgrade
 *
 * @return
 *   - ESP_OK
 *   - ESP_ERR_INVALID_ARG
 *   - ESP_ERR_NOT_SUPPORTED
 */
esp_err_t espnow_ota_responder_get_status(espnow_ota_status_t *status);

/**
 * @brief Stop upgrading
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_ota_responder_stop();

/**
 * @brief Start upgrading
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_ota_responder_start(const espnow_ota_config_t *config);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
