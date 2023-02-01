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

#ifndef __ESPNOW_DEBUG_FLASH_H__
#define __ESPNOW_DEBUG_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Initialize espnow log flash
 *        Find and get log flash partition which size is not smaller than LOG_FLASH_FILE_MAX_SIZE. 
 *        Create several files under the spiffs folder. 
 *        Open the file in the next step for the storage of the log data.
 * 
 * @return
 *      - ESP_OK
 */
esp_err_t espnow_log_flash_init();

/**
 * @brief Deinit log flash
 *        Log flash operation will be stopped.
 *
 * @return
 *      - ESP_OK
 */
esp_err_t espnow_log_flash_deinit();

/**
 * @brief Erase the flash when the log partition is full
 *
 * @return
 *      - ESP_OK
 */
esp_err_t espnow_log_flash_erase();

/**
 * @brief Write memory log data in flash
 *
 * @note Don't include timestamp in interface input data
 *
 * @param[in]  data  log data to be stored in flash's spiffs files
 * @param[in]  size  size of the data in bytes
 * @param[in]  level  level of the log
 *
 * @return
 *      - ESP_OK
 */
esp_err_t espnow_log_flash_write(const char *data, size_t size, esp_log_level_t level);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
#endif /**< __ESPNOW_DEBUG_FLASH_H__ */
