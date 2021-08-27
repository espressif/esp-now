// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

#include <stdint.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include <esp_timer.h>
#include <esp_system.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

#include "esp_utils.h"
#include "esp_storage.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/rtc.h"
#endif

#define REBOOT_RECORD_KEY   "reboot_record"
#define CONFIG_UNBROKEN_RECORD_TASK_DEFAULT_PRIOTY (ESP_TASK_MAIN_PRIO + 1)

typedef struct  {
    size_t total_count;
    size_t unbroken_count;
    RESET_REASON reason;
} esp_reboot_record_t;

static const char *TAG = "esp_reboot";
static esp_reboot_record_t g_reboot_record = {0};

static void esp_reboot_cb(void *priv)
{
    esp_restart();
}

esp_err_t esp_reboot(uint32_t wait_ms)
{
    static esp_timer_handle_t s_reboot_timer_handle = NULL;

    if (s_reboot_timer_handle) {
        return ESP_FAIL;
    }

    esp_timer_create_args_t timer_cfg = {
        .name = "esp_reboot_tm",
        .callback = esp_reboot_cb,
        .dispatch_method = ESP_TIMER_TASK,
    };

    if (esp_timer_create(&timer_cfg, &s_reboot_timer_handle) == ESP_OK) {
        return esp_timer_start_once(s_reboot_timer_handle, pdMS_TO_TICKS(wait_ms) * 1000U);
    }

    return ESP_FAIL;
}

static void esp_reboot_count_erase_timercb(void *priv)
{
    g_reboot_record.unbroken_count = 0;
    esp_storage_set(REBOOT_RECORD_KEY, &g_reboot_record, sizeof(esp_reboot_record_t));

    ESP_LOGI("restart_func", "num: %d, reason: %d, crash: %d",
             esp_reboot_total_count(), g_reboot_record.reason, esp_reboot_is_exception(false));

}

static esp_err_t esp_reboot_unbroken_init()
{
    esp_err_t err          = ESP_OK;
    g_reboot_record.reason = rtc_get_reset_reason(0);

    esp_storage_init();
    esp_storage_get(REBOOT_RECORD_KEY, &g_reboot_record, sizeof(esp_reboot_record_t));

    g_reboot_record.total_count++;

    /**< If the device reboots within the instruction time,
         the event_mdoe value will be incremented by one */
    if (g_reboot_record.reason != DEEPSLEEP_RESET
            && g_reboot_record.reason != RTCWDT_BROWN_OUT_RESET) {
        g_reboot_record.unbroken_count++;
        ESP_LOGD(TAG, "reboot unbroken count: %d", g_reboot_record.unbroken_count);
    } else {
        g_reboot_record.unbroken_count = 1;
        ESP_LOGW(TAG, "reboot reason: %d", g_reboot_record.reason);
    }

    err = esp_storage_set(REBOOT_RECORD_KEY, &g_reboot_record, sizeof(esp_reboot_record_t));
    ESP_ERROR_RETURN(err != ESP_OK, err, "Save the number of reboots within the set time");

    esp_timer_handle_t time_handle   = NULL;
    esp_timer_create_args_t timer_cfg = {
        .name = "reboot_count_erase",
        .callback = esp_reboot_count_erase_timercb,
        .dispatch_method = ESP_TIMER_TASK,
    };

    err = esp_timer_create(&timer_cfg, &time_handle);
    ESP_ERROR_RETURN(err != ESP_OK, err, "esp_timer_create");

    err = esp_timer_start_once(time_handle, pdMS_TO_TICKS(CONFIG_REBOOT_UNBROKEN_INTERVAL_TIMEOUT) * 1000U);
    ESP_ERROR_RETURN(err != ESP_OK, err, "esp_timer_start_once");

    return ESP_OK;
}

static void reboot_unbroken_record_task(void *arg)
{
    esp_reboot_unbroken_init();

    if (CONFIG_REBOOT_UNBROKEN_FALLBACK_COUNT &&
            esp_reboot_unbroken_count() >= CONFIG_REBOOT_UNBROKEN_FALLBACK_COUNT) {
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }

    ESP_LOGD(TAG, "version_fallback_task exit");

    vTaskDelete(NULL);
}

__attribute((constructor)) static esp_err_t esp_reboot_unbroken_record()
{
    /**
     * @brief Wait for high-priority tasks to run first
     */
    xTaskCreate(reboot_unbroken_record_task, "reboot_unbroken_record", 3 * 1024,
                NULL, CONFIG_UNBROKEN_RECORD_TASK_DEFAULT_PRIOTY, NULL);

    return ESP_OK;
}

int esp_reboot_unbroken_count()
{
    return g_reboot_record.unbroken_count;
}

int esp_reboot_total_count()
{
    return g_reboot_record.total_count;
}

bool esp_reboot_is_exception(bool erase_coredump)
{
    esp_err_t ret        = ESP_OK;
    ssize_t coredump_len = 0;

    const esp_partition_t *coredump_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    ESP_ERROR_RETURN(!coredump_part, false, "");

    ret = esp_partition_read(coredump_part, 0, &coredump_len, sizeof(ssize_t));
    ESP_ERROR_RETURN(ret, false, "esp_partition_read fail");

    if (coredump_len <= 0) {
        return false;
    }

    if (erase_coredump) {
        /**< erase all coredump partition */
        ret = esp_partition_erase_range(coredump_part, 0, coredump_part->size);
        ESP_ERROR_RETURN(ret, false, "esp_partition_erase_range fail");
    }

    return true;
}
