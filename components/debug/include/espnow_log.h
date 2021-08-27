// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
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

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"

#include "esp_utils.h"
#include "esp_mem.h"

/**
 *@brief Configuration mdebug print enable, whether the output information according to the client needs to know.
 *       please assign CONFIG_DEBUG_LOG_PRINTF_ENABLE a value.
 *
 * @note CONFIG_DEBUG_LOG_PRINTF_ENABLE = 1  enable
 *       CONFIG_DEBUG_LOG_PRINTF_ENABLE = 0  disable
 */
// #define CONFIG_DEBUG_LOG_PRINTF_ENABLE
#ifdef CONFIG_DEBUG_LOG_PRINTF_ENABLE
#define DEBUG_LOG_PRINTF(fmt, ...) printf("D [%s, %d]: " fmt, TAG, __LINE__, __VA_ARGS__)
#else
#define DEBUG_LOG_PRINTF(fmt, ...)
#endif

#define DEBUG_LOG_MALLOC malloc
#define DEBUG_LOG_FREE   free

#define ESP_EVENT_ESPNOW_LOG_FLASH_FULL      (ESP_EVENT_ESPNOW_DEBUG_BASE + 1)

typedef esp_err_t (*espnow_log_custom_write_cb)(const char *data, size_t size, const char *tag, esp_log_level_t level);

/**
 * @brief Log sending configuration
 */
typedef struct {
    esp_log_level_t log_level_uart;
    esp_log_level_t log_level_flash;
    esp_log_level_t log_level_espnow;
    esp_log_level_t log_level_custom;
    espnow_log_custom_write_cb log_custom_write;
} espnow_log_config_t;

/**
 * @brief  Get the configuration of the log during wireless debugging
 *
 * @param  config The configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t espnow_log_get_config(espnow_log_config_t *config);

/**
 * @brief  Set the configuration of the log during wireless debugging
 *
 * @param  config The configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t espnow_log_set_config(const espnow_log_config_t *config);

/**
 * @brief Init log mdebug
 *        - Set log mdebug configuration
 *        - Create log mdebug task
 *
 * @return
 *     - ESP_OK
 */
esp_err_t espnow_log_init(const espnow_log_config_t *config);

/**
 * @brief De-initialize log mdebug
 *      Call this once when done using log mdebug functions
 *
 * @return
 *     - ESP_OK
 */
esp_err_t espnow_log_deinit(void);

/**
 * @brief Read memory data in flash
 *
 * @param data  Data from the flash's spiffs files in the log
 * @param size  Size from the flash's spiffs files in the log
 *
 * @return
 *      - ESP_OK
 *      - read_size
 */
esp_err_t espnow_log_flash_read(char *data, size_t *size);

/**
 * @brief Create files size,For the data to be stored in the file
 *      for subsequent calls.paramters DEBUG_FLASH_FILE_MAX_NUM
 *      if files sizes change.
 *
 * @return
 *      - size
 */
size_t espnow_log_flash_size(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
