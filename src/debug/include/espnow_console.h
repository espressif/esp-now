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
 * @brief Console configuration
 */
typedef struct espnow_console_config_s {
    struct {
        bool uart;                  /**< Receiving command from uart */
        bool espnow;                /**< Receiving command from espnow. Debug facility without access
                                         control of its own: every received DEBUG_COMMAND frame is executed
                                         (restart, reset, wifi_config, ota, ...). Unless the frames are
                                         encrypted (CONFIG_ESPNOW_DEBUG_SECURITY and sec_enable) and plaintext
                                         ones are rejected (CONFIG_ESPNOW_SEC_REJECT_PLAINTEXT), any device in
                                         range can run them; espnow_console_init() then logs a warning. Debug
                                         use only: even encrypted, a recorded command frame (e.g. `reset`, which
                                         erases NVS) can be replayed without the key. The recommended
                                         configuration keeps it false. */
    } monitor_command;              /**< Command monitor configuration */
    struct {
        const char *base_path;      /**< Path where partition should be registered (e.g. "/spiflash") */
        const char *partition_label;/**< Label of the partition which should be used */
    } store_history;                /**< Command story history configuration */
} espnow_console_config_t;

/**
 * @brief   Initialize console module
 *          - Initialize the console
 *          - Register help commands
 *          - Initialize filesystem
 *          - Create console handle task
 *
 * @note With monitor_command.espnow set, the DEBUG_COMMAND handler is registered
 *       and ESP_OK returned even when the frames are not protected; only an
 *       error is logged. The application must keep monitor_command.espnow
 *       false in production builds.
 *
 * @param[in]  config  pointer to the console configuration structure
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
 * @brief Register Common commands
 */
void espnow_console_commands_register();

#ifdef __cplusplus
}
#endif /**< _cplusplus */
