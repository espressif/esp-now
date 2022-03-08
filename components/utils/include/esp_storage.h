// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 
 * @brief Intialise ESP Storage
 *
 * @return
 *     - ESP_FAIL
 *     - ESP_OK
 */
esp_err_t esp_storage_init(void);

/**
 * @brief Save the information with given key
 *
 * @param[in]  key    key name. Maximal length is 15 characters. Shouldn't be empty.
 * @param[in]  value  the value to set
 * @param[in]  length length of binary value to set, in bytes; Maximum length is
 *             1984 bytes or (508000 bytes or (97.6% of the partition size - 4000) bytes
 *             whichever is lower, in case multi-page blob support is enabled).
 *
 * @return
 *     - ESP_FAIL
 *     - ESP_OK
 */
esp_err_t esp_storage_set(const char *key, const void *value, size_t length);

/**
 * @brief Load the information with given key
 *
 * @param[in]  key  the corresponding key of the information that want to load
 * @param[out]  value  the corresponding value of key
 * @param[in]  length  the length of the value, can be 0 or no less than the value length
 *
 * @return
 *     - ESP_FAIL
 *     - ESP_OK
 */
esp_err_t esp_storage_get(const char *key, void *value, size_t length);

/**
 * @brief  Erase the information with given key
 *
 * @param[in]  key  the corresponding key of the information that want to erase
 *
 * @return
 *     - ESP_FAIL
 *     - ESP_OK
 */
esp_err_t esp_storage_erase(const char *key);

#ifdef __cplusplus
}
#endif
