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

#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include <esp_err.h>
#include "esp_log.h"

#include "esp_mem.h"
#include "esp_storage.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct esp_time_config {
    /** If not specified, then 'CONFIG_ESP_SNTP_SERVER_NAME' is used as the SNTP server. */
    char *sntp_server_name;
} esp_time_config_t;

/**
 * Macro which can be used to check the error code,
 * and terminate the program in case the code is not ESP_OK.
 * Prints the error code, error location, and the failed statement to serial output.
 *
 * Disabled if assertions are disabled.
 */
#define ESP_ERROR_RETURN(con, err, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "[%s, %d] <%s> " format, __func__, __LINE__,esp_err_to_name(err), ##__VA_ARGS__); \
            return err; \
        } \
    } while(0)

/**
 * @brief Macro serves similar purpose as ``assert``, except that it checks `esp_err_t`
 *        value rather than a `bool` condition. If the argument of `ESP_ERROR_ASSERT`
 *        is not equal `ESP_OK`, then an error message is printed on the console,
 *         and `abort()` is called.
 *
 * @note If `IDF monitor` is used, addresses in the backtrace will be converted
 *       to file names and line numbers.
 *
 * @param[in]  err [description]
 * @return         [description]
 */
#define ESP_ERROR_ASSERT(err) do { \
        esp_err_t __err_rc = (err); \
        if (__err_rc != ESP_OK) { \
            ESP_LOGW(TAG, "[%s, %d] <%s> ESP_ERROR_ASSERT failed, at 0x%08x, expression: %s", \
                     __func__, __LINE__, esp_err_to_name(__err_rc), (intptr_t)__builtin_return_address(0) - 3, __ASSERT_FUNC); \
            assert(0 && #err); \
        } \
    } while(0)

#define ESP_ERROR_GOTO(con, lable, format, ...) do { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "[%s, %d]" format, __func__, __LINE__, ##__VA_ARGS__); \
            goto lable; \
        } \
    } while(0)

#define ESP_ERROR_CONTINUE(con, format, ...) { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "[%s, %d]: " format, __func__, __LINE__, ##__VA_ARGS__); \
            continue; \
        } \
    }

#define ESP_ERROR_BREAK(con, format, ...) { \
        if (con) { \
            if(*format != '\0') \
                ESP_LOGW(TAG, "[%s, %d]: " format, __func__, __LINE__, ##__VA_ARGS__); \
            break; \
        } \
    }

#define ESP_PARAM_CHECK(con) do { \
        if (!(con)) { \
            ESP_LOGE(TAG, "[%s, %d]: <ESP_ERR_INVALID_ARG> !(%s)", __func__, __LINE__, #con); \
            return ESP_ERR_INVALID_ARG; \
        } \
    } while(0)

/**
 * @brief Reboot the chip after a delay
 *
 * This API just starts an esp_timer and executes a reboot from that.
 * Useful if you want to reboot after a delay, to allow other tasks to finish
 * their operations (Eg. MQTT publish to indicate OTA success)
 *
 * @param[in]  wait_ticks  time in ticks after which the chip should reboot
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t esp_reboot(TickType_t wait_ticks);

/**
 * @brief Get the number of consecutive restarts
 *
 * @return
 *     - count
 */
int esp_reboot_unbroken_count(void);

/**
 * @brief Get the number of restarts
 *
 * @return
 *     - count
 */
int esp_reboot_total_count(void);

/**
 * @brief Determine if the restart is caused by an exception.
 *
 * @param[in]  erase_coredump  erase all coredump partition
 * 
 * @return
 *     - true
 *     - false
 */
bool esp_reboot_is_exception(bool erase_coredump);

/**
 * @brief Initialize time synchronization
 *
 * This API initializes SNTP for time synchronization.
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t esp_timesync_start(void);

/**
 * @brief Check if current time is updated
 *
 * This API checks if the current system time is updated against the reference time of 1-Jan-2020.
 *
 * @return
 *      - true if time is updated
 *      - false if time is not updated
 */
bool esp_timesync_check(void);

/**
 * @brief Wait for time synchronization
 *
 * This API waits for the system time to be updated against the reference time of 1-Jan-2020.
 * This is a blocking call.
 *
 * @param[in]  wait_ms  number of ticks to wait for time synchronization.
 *
 * @return
 *      - ESP_OK
 */
esp_err_t esp_timesync_wait(uint32_t wait_ms);

/**
 * @brief Interval printing system information
 *
 * @param[in]  interval_ms  interval of printing system log information
 *
 */
void esp_print_system_info(uint32_t interval_ms);

/**
 * @brief Convert MAC from string to hex
 *
 * @param[in]  mac_str  mac address string in format MACSTR
 * @param[out]  mac_hex  mac address array in hex
 * 
 * @return
 *      - NULL: fail
 *      - mac_hex: success
 */
uint8_t *mac_str2hex(const char *mac_str, uint8_t *mac_hex);

#ifdef __cplusplus
}
#endif
