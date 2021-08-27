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
#include <esp_log.h>
#include "lwip/apps/sntp.h"
#include <esp_utils.h>

#define REF_TIME        1577808000 /* 2020-01-01 00:00:00 */
#define DEFAULT_TICKS   (2000 / portTICK_PERIOD_MS) /* 2 seconds in ticks */
#define CONFIG_ESP_SNTP_SERVER_NAME "pool.ntp.org"

static bool g_init_done = false;
static const char *TAG = "esp_timesync";

esp_err_t esp_timesync_start()
{
    if (sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP already initialized.");
        g_init_done = true;
        return ESP_OK;
    }

    char *sntp_server_name = CONFIG_ESP_SNTP_SERVER_NAME;

    ESP_LOGI(TAG, "Initializing SNTP. Using the SNTP server: %s", sntp_server_name);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, sntp_server_name);

    sntp_init();
    g_init_done = true;

    return ESP_OK;
}

bool esp_timesync_check(void)
{
    time_t now;
    time(&now);

    if (now > REF_TIME) {
        return true;
    }

    return false;
}

esp_err_t esp_timesync_wait(TickType_t ticks_to_wait)
{
    if (!g_init_done) {
        ESP_LOGW(TAG, "Time sync not initialised using 'esp_timesync_start'");
    }

    ESP_LOGW(TAG, "Waiting for time to be synchronized. This may take time.");
    uint32_t ticks_remaining = ticks_to_wait;
    uint32_t ticks = DEFAULT_TICKS;

    while (ticks_remaining > 0) {
        if (esp_timesync_check() == true) {
            break;
        }

        ESP_LOGD(TAG, "Time not synchronized yet. Retrying...");
        ticks = ticks_remaining < DEFAULT_TICKS ? ticks_remaining : DEFAULT_TICKS;
        ticks_remaining -= ticks;
        vTaskDelay(ticks);
    }

    /* Check if ticks_to_wait expired and time is not synchronized yet. */
    if (esp_timesync_check() == false) {
        ESP_LOGE(TAG, "Time not synchronized within the provided ticks: %u", ticks_to_wait);
        return ESP_FAIL;
    }

    /* Get current time */
    struct tm timeinfo;
    char strftime_buf[64];
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current UTC time is: %s", strftime_buf);
    return ESP_OK;
}
