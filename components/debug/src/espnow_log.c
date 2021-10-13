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

#include "esp_wifi.h"
#include "esp_console.h"

#include "espnow.h"
#include "espnow_log.h"
#include "espnow_log_flash.h"
#include <sys/param.h>

#define DEBUG_LOG_STORE_KEY               "log_config"
#define DEBUG_LOG_QUEUE_SIZE              (16)
#define DEBUG_LOG_TIMEOUT_MS              (30 * 1000)
#define DEBUG_LOG_QUEUE_BUFFER_MAX_SIZE   (10 * 1024)

#define CONFIG_DEBUG_TASK_DEFAULT_PRIOTY   1
#define CONFIG_DEBUG_TASK_PINNED_TO_CORE   0
#define CONFIG_DEBUG_LOG_MAX_SIZE          1024  /**< Set log length size */

static const char *TAG                   = "espnow_log";
static xQueueHandle g_log_queue          = NULL;
static bool g_log_init_flag              = false;
static espnow_log_config_t *g_log_config = NULL;

typedef struct log_info {
    const char *tag;
    esp_log_level_t level;
    size_t size;
    char *data;
} log_info_t;

esp_err_t espnow_log_get_config(espnow_log_config_t *config)
{
    ESP_PARAM_CHECK(config);
    ESP_ERROR_RETURN(!g_log_init_flag, ESP_ERR_NOT_SUPPORTED, "log debugging is not initialized");

    memcpy(config, g_log_config, sizeof(espnow_log_config_t));
    return ESP_OK;
}

esp_err_t espnow_log_set_config(const espnow_log_config_t *config)
{
    ESP_PARAM_CHECK(config);
    ESP_ERROR_RETURN(!g_log_init_flag, ESP_ERR_NOT_SUPPORTED, "log debugging is not initialized");

    esp_err_t ret = ESP_OK;

    memcpy(g_log_config, config, sizeof(espnow_log_config_t));

    return ret;
}

static void espnow_log_send_task(void *arg)
{
    log_info_t *log_info = NULL;

    for (; g_log_config;) {
        if (xQueueReceive(g_log_queue, &log_info, pdMS_TO_TICKS(DEBUG_LOG_TIMEOUT_MS)) != pdPASS) {
            continue;
        }

        if (log_info->level <= g_log_config->log_level_flash) {
            espnow_log_flash_write(log_info->data, log_info->size, log_info->level); /**< Write log data to flash */
        }

        if (strcasecmp(log_info->tag, "espnow") && log_info->level <= g_log_config->log_level_espnow) {
            log_info->size = MIN(ESPNOW_DATA_LEN - 1, log_info->size) + 1;
            log_info->data[log_info->size - 1] = '\0';
            espnow_send(ESPNOW_TYPE_DEBUG_LOG, ESPNOW_ADDR_BROADCAST, log_info->data, log_info->size, NULL, portMAX_DELAY);
        }

        if (log_info->level <= g_log_config->log_level_custom) {
            if (g_log_config->log_custom_write) {
                g_log_config->log_custom_write(log_info->data, log_info->size, log_info->tag, log_info->level); /**< Write log data to custom */
            }
        }

        DEBUG_LOG_FREE(log_info->data);
        DEBUG_LOG_FREE(log_info);
    }

    vTaskDelete(NULL);
}

void __real_esp_log_writev(esp_log_level_t level,
                           const char *tag,
                           const char *format,
                           va_list args);

void __wrap_esp_log_writev(esp_log_level_t level,
                           const char *tag,
                           const char *format,
                           va_list args)
{
    if (!g_log_config || level <= g_log_config->log_level_uart) {
        __real_esp_log_writev(level, tag, format, args);
    }

    if (!g_log_config || (level > g_log_config->log_level_espnow
                          && level > g_log_config->log_level_flash
                          && level > g_log_config->log_level_custom)) {
        return;
    }

    log_info_t *log_info = DEBUG_LOG_MALLOC(sizeof(log_info_t));

    log_info->size  = vasprintf(&log_info->data, format, args);
    log_info->level = level;
    log_info->tag   = tag;

    if (xQueueSend(g_log_queue, &log_info, 0) == pdFALSE) {
        DEBUG_LOG_FREE(log_info->data);
        DEBUG_LOG_FREE(log_info);
    }
}

void __wrap_esp_log_write(esp_log_level_t level,
                          const char *tag,
                          const char *format, ...)
{
    va_list list;
    va_start(list, format);

    esp_log_writev(level, tag, format, list);
    va_end(list);
}

esp_err_t espnow_log_init(const espnow_log_config_t *config)
{
    if (g_log_init_flag) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    g_log_queue = xQueueCreate(DEBUG_LOG_QUEUE_SIZE, sizeof(log_info_t *));
    ESP_ERROR_RETURN(!g_log_queue, ESP_FAIL, "g_log_queue create fail");

    g_log_config = ESP_CALLOC(1, sizeof(espnow_log_config_t));
    ESP_ERROR_RETURN(!g_log_config, ESP_ERR_NO_MEM, "");
    memcpy(g_log_config, config, sizeof(espnow_log_config_t));

    if (config->log_level_flash != ESP_LOG_NONE) {
        ret = espnow_log_flash_init();
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_log_flash_init");
    }

    xTaskCreatePinnedToCore(espnow_log_send_task, "espnow_log_send", 3 * 1024,
                            NULL, CONFIG_DEBUG_TASK_DEFAULT_PRIOTY,
                            NULL, CONFIG_DEBUG_TASK_PINNED_TO_CORE);

    ESP_LOGI(TAG, "log initialized successfully");

    g_log_init_flag = true;

    return ESP_OK;
}

esp_err_t espnow_log_deinit()
{
    if (g_log_init_flag) {
        return ESP_FAIL;
    }

    log_info_t *log_data = NULL;

    while (xQueueReceive(g_log_queue, &log_data, 0)) {
        DEBUG_LOG_FREE(log_data);
        DEBUG_LOG_FREE(log_data->data);
    }

    espnow_log_flash_deinit();

    g_log_init_flag = false;
    DEBUG_LOG_FREE(g_log_config);

    return ESP_OK;
}
