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

#pragma once

#include "argtable3/argtable3.h"
#include "esp_console.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief 
 */
typedef struct {
    struct {
        bool uart;
        bool espnow;
    } monitor_command;
    struct {
        const char *base_path;
        const char *partition_label;
    } store_history;
} espnow_console_config_t;

/**
 * @brief   Initialize console module
 *          - Initialize the console
 *          - Register help commands
 *          - Initialize filesystem
 *          - Create console handle task
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t espnow_console_init(const espnow_console_config_t *config);

/**
 * @brief   De-initialize console module
 *          Call this once when done using console module functions
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t espnow_console_deinit(void);

/**
 * @brief Common commands
 */
void espnow_console_commands_register();

#ifdef __cplusplus
}
#endif /**< _cplusplus */
