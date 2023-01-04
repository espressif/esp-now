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
 *@brief Configuration of debug log stored in flash.
 *       Whether to output information according to the client needs.
 *       Please assign CONFIG_DEBUG_LOG_PRINTF_ENABLE a value.
 *
 * @note CONFIG_DEBUG_LOG_PRINTF_ENABLE = 1  enable
 *       CONFIG_DEBUG_LOG_PRINTF_ENABLE = 0  disable
 */
#ifdef CONFIG_DEBUG_LOG_PRINTF_ENABLE
#define DEBUG_LOG_PRINTF(fmt, ...) printf("D [%s, %d]: " fmt, TAG, __LINE__, __VA_ARGS__)
#else
#define DEBUG_LOG_PRINTF(fmt, ...)
#endif

#define DEBUG_LOG_MALLOC malloc
#define DEBUG_LOG_FREE   free

/**
 * @brief Enumerated list of debug event id
 */
#define ESP_EVENT_ESPNOW_LOG_FLASH_FULL      (ESP_EVENT_ESPNOW_DEBUG_BASE + 1)

/**
 * @brief  The log callback function
 *
 * @attention Each time a log is sent, the callback function will be called if defined.
 *
 * @param[in]  data  the log data
 * @param[in]  size  the log data size in bytes
 * @param[in]  tag  the log tag
 * @param[in]  level  the log level
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
typedef esp_err_t (*espnow_log_custom_write_cb)(const char *data, size_t size, const char *tag, esp_log_level_t level);

/**
 * @brief Log sending configuration
 */
typedef struct {
    esp_log_level_t log_level_uart;                 /**< Level of log printed from uart */
    esp_log_level_t log_level_flash;                /**< Level of log stored in flash */
    esp_log_level_t log_level_espnow;               /**< Level of log sent from espnow */
    esp_log_level_t log_level_custom;               /**< Level of log defined by customer */
    espnow_log_custom_write_cb log_custom_write;    /**< Customer defined log callback function */
} espnow_log_config_t;

/**
 * @brief  Get the configuration of the log during wireless debugging
 *
 * @param[out]  config  the configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t espnow_log_get_config(espnow_log_config_t *config);

/**
 * @brief  Set the configuration of the log during wireless debugging
 *
 * @param[in]  config  the configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t espnow_log_set_config(const espnow_log_config_t *config);

/**
 * @brief Initialize log debug
 *        - Set log debug configuration
 *        - Create the log sendinng task
 *
 * @param[in]  config  the configuration of the log
 * 
 * @return
 *     - ESP_OK
 */
esp_err_t espnow_log_init(const espnow_log_config_t *config);

/**
 * @brief De-initialize log debug
 *        Call this once when done using log debug functions
 *
 * @return
 *     - ESP_OK
 */
esp_err_t espnow_log_deinit(void);

/**
 * @brief Read memory log data in flash
 *
 * @param[out]  data  log data read from the flash's spiffs files
 * @param[inout]  size  size of the read data in bytes
 *
 * @return
 *      - ESP_OK
 *      - ESP_FAIL
 */
esp_err_t espnow_log_flash_read(char *data, size_t *size);

/**
 * @brief Get the size of log stored in flash
 * 
 * @return
 *      - size
 */
size_t espnow_log_flash_size(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
