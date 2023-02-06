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

#ifndef __SDCARD_H__
#define __SDCARD_H__

#include "espnow_utils.h"

#include "driver/gpio.h"

#ifdef  __cplusplus
extern "C" {
#endif

typedef enum {
    FILE_TYPE_NONE,
    FILE_TYPE_HEX,
    FILE_TYPE_BIN,
    FILE_TYPE_STRING,
    FILE_TYPE_BASE64,
} file_format_t;


typedef struct {
    gpio_num_t gpio_num_cmd;
    gpio_num_t gpio_num_d0;
    gpio_num_t gpio_num_d1;
    gpio_num_t gpio_num_d2;
    gpio_num_t gpio_num_d3;
} sdcard_config_t;


/**
 * @brief  Initialize sdcard, Using SDMMC peripheral
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_init(const sdcard_config_t *config);

/**
 * @brief  Remove file stored in sdcard
 *
 * @param  file_name  file name
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_remove_file(const char *file_name);

/**
 * @brief  Rename file stored in sdcard
 *
 * @param  file_name_old  file old name
 * @param  file_name_new  file new name
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_rename_file(const char *file_name_old, const char *file_name_new);

/**
 * @brief  List all matching files
 *
 * @param  file_name  File match string,eg: *, *.log.
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_list_file(const char *file_name);

/**
 * @brief  Print special file content
 *
 * @param  file_name  file name
 * @param  type  file type
 * @param  size  file size
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_print_file(const char *file_name, file_format_t type, ssize_t size);

/**
 * @brief  Write file content to sdcard
 *
 * @param  file_name  file name
 * @param  offset  write offset, if offset == UINT32_MAX, add data at end of file.
 * @param  data  writen data
 * @param  size  data length
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_write_file(const char *file_name, uint32_t offset, const void *data, size_t size);

/**
 * @brief  Read file content from sdcard
 *
 * @param  file_name  file name
 * @param  offset  write offset
 * @param  data  read data
 * @param  size  data length
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t sdcard_read_file(const char *file_name, uint32_t offset, uint8_t *data, size_t *size);

/**
 * @brief  Check if sdcard is mounted
 *
 * @return
 *     - true
 *     - false
 */
bool sdcard_is_mount();

#ifdef __cplusplus
}
#endif

#endif/*!< __SDCARD_H__ */
