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

#include "espnow_mem.h"
#include "espnow_utils.h"

/**
 *@brief Configuration of debug log stored in flash.
 *       Whether to output information according to the client needs.
 *       Please assign CONFIG_ESPNOW_DEBUG_LOG_PRINTF_ENABLE a value.
 *
 * @note CONFIG_ESPNOW_DEBUG_LOG_PRINTF_ENABLE = 1  enable
 *       CONFIG_ESPNOW_DEBUG_LOG_PRINTF_ENABLE = 0  disable
 */
#ifdef CONFIG_ESPNOW_DEBUG_LOG_PRINTF_ENABLE
#define DEBUG_LOG_PRINTF(fmt, ...) printf("D [%s, %d]: " fmt, TAG, __LINE__, __VA_ARGS__)
#else
#define DEBUG_LOG_PRINTF(fmt, ...)
#endif

#define DEBUG_LOG_MALLOC malloc
#define DEBUG_LOG_FREE   free

#ifdef CONFIG_ESPNOW_ALL_SECURITY
#define CONFIG_ESPNOW_DEBUG_SECURITY 1
#else
#ifndef CONFIG_ESPNOW_DEBUG_SECURITY
#define CONFIG_ESPNOW_DEBUG_SECURITY 0
#endif
#endif

/**
 * @brief Enumerated list of debug event id
 */
#define ESP_EVENT_ESPNOW_LOG_FLASH_FULL      (ESP_EVENT_ESPNOW_DEBUG_BASE + 1)

/**
 * @brief  The log callback function
 *
 * @attention Each time a log is sent, the callback function will be called if defined.
 *            Logs are delivered asynchronously from a dedicated task; `tag` is the
 *            pointer passed to the log call and is not copied, so, as with
 *            ESP-IDF logging, it must be a static constant string. A log issued
 *            with a NULL tag is delivered with an empty string.
 *
 * @param[in]  data  the log data: the whole line, NUL-terminated
 * @param[in]  size  the log data size in bytes, not counting the terminating
 *                   NUL. It is the full length of the line whether or not the
 *                   ESP-NOW sink is enabled; only the copy sent over ESP-NOW
 *                   is cut to one frame (ESPNOW_DATA_LEN).
 * @param[in]  tag  the log tag ("" when the log was issued without a tag)
 * @param[in]  level  the log level
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
typedef esp_err_t (*espnow_log_custom_write_cb)(const char *data, size_t size, const char *tag, esp_log_level_t level);

/**
 * @brief Log sending configuration
 *
 * @note The four levels below are applied after ESP-IDF's own filtering: a line
 *       only reaches a sink if the SDK would also print it, i.e. it passes the
 *       compile-time LOG_LOCAL_LEVEL, the tag's run-time level set with
 *       esp_log_level_set() (console `log -t <tag> -l <level>`) and, when
 *       CONFIG_LOG_MASTER_LEVEL is enabled, the master level. Silencing a tag
 *       therefore silences it on the UART, over ESP-NOW, in flash and in the
 *       custom callback alike.
 */
typedef struct espnow_log_config_s {
    esp_log_level_t log_level_uart;                 /**< Level of log printed from uart */
    esp_log_level_t log_level_flash;                /**< Level of log stored in flash. Each line is written to the log
                                                         partition and updates the file table in the log NVS partition.
                                                         Lines can be triggered by unauthenticated frames in range;
                                                         CONFIG_ESPNOW_DEBUG_LOG_FLASH_ERASE_MIN_INTERVAL_MS (default 0,
                                                         no bound) limits how often the sink rotates (erases) and drops
                                                         lines in between; see that option for the wear bound. In a
                                                         deployment set it and keep this level at ESP_LOG_WARN or lower. */
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
 *        - Create the log sending task
 *
 * @note Can be called again after espnow_log_deinit(). espnow_log_init() and
 *       espnow_log_deinit() must not run at the same time on two tasks.
 *
 * @param[in]  config  the configuration of the log
 * 
 * @return
 *     - ESP_OK
 *     - ESP_FAIL: already initialized
 */
esp_err_t espnow_log_init(const espnow_log_config_t *config);

/**
 * @brief De-initialize log debug
 *        Call this once when done using log debug functions
 *
 * @note Stops the flash, ESP-NOW and custom sinks and gives the UART output
 *       back to ESP-IDF (log_level_uart no longer applies). Other tasks may
 *       keep logging while it runs. It returns after the log sending task has
 *       exited, i.e. after the line being delivered (including a running
 *       log_custom_write) is finished; lines still queued at that point are
 *       discarded and log_custom_write is not called again. Blocks, so it
 *       cannot be called from an ISR, and not from log_custom_write itself.
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL: not initialized
 *     - ESP_ERR_INVALID_STATE: called from log_custom_write
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
