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
#include "esp_idf_version.h"
#include "esp_attr.h"

#include "espnow.h"
#include "espnow_log.h"
#include "espnow_log_flash.h"
#include <sys/param.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
/* The per-tag level check esp_log_va() applies before printing. */
#include "esp_private/log_level.h"
#if ESP_LOG_VERSION == 2
/* Same timestamp text as esp_log_format() (RTOS ticks or system time), and the
 * run-time constrained-environment check esp_log_va() applies before formatting. */
#include "esp_private/log_timestamp.h"
#include "esp_private/log_util.h"
#endif
#endif

#define DEBUG_LOG_STORE_KEY               "log_config"
#define DEBUG_LOG_QUEUE_SIZE              (16)
#define DEBUG_LOG_TIMEOUT_MS              (30 * 1000)
#define DEBUG_LOG_QUEUE_BUFFER_MAX_SIZE   (10 * 1024)

#define CONFIG_DEBUG_TASK_DEFAULT_PRIOTY   1
#define CONFIG_DEBUG_TASK_PINNED_TO_CORE   0
#define CONFIG_DEBUG_LOG_MAX_SIZE          1024  /**< Set log length size */

static const char *TAG                   = "espnow_log";
static QueueHandle_t g_log_queue          = NULL;
static bool g_log_init_flag              = false;
/* Valid from espnow_log_init() to espnow_log_deinit(), like g_log_queue. Only
 * the send task and holders of a reference (see g_log_users) may use the two. */
static espnow_log_config_t *g_log_config = NULL;
/* Loop condition of the send task; separate from g_log_config so the task can
 * be created before the wrappers are armed. */
static volatile bool g_log_task_run      = false;
/* Set by the send task as its last access to anything of this module, right
 * before it deletes itself; espnow_log_deinit() waits for it. */
static volatile bool g_log_task_done     = true;
static TaskHandle_t g_log_task           = NULL;
/* Users of g_log_config and g_log_queue outside the send task: a capture in
 * a wrapper (any task, either core) and espnow_log_get_config() /
 * espnow_log_set_config(). Each takes a reference for as long as it uses the
 * two, and only gets one while g_log_users_on is set. espnow_log_deinit()
 * clears the flag and waits for the count to reach 0 before it deletes the
 * queue and frees the configuration, so no user is left holding either.
 * Both are accessed with sequentially consistent atomics: a user increments
 * and then reads the flag, deinit clears the flag and then reads the count,
 * so at least one of the two sees the other. */
static bool g_log_users_on               = false;
static uint32_t g_log_users              = 0;
/* Copy of g_log_config->log_level_uart for the cache-off path of
 * __wrap_esp_log(). g_log_config comes from ESP_CALLOC(), which may return
 * external RAM (CONFIG_SPIRAM_USE_MALLOC, CONFIG_ESPNOW_MEM_ALLOCATION_SPIRAM)
 * that cannot be read while the cache is disabled, so that path reads only
 * this byte, which DRAM_ATTR keeps in internal RAM. A single byte needs no
 * lock: a reader sees the old or the new level, never a mix. */
#define ESPNOW_LOG_UART_NO_GATE          UINT8_MAX  /**< not initialized: print everything */
static DRAM_ATTR volatile uint8_t g_log_level_uart = ESPNOW_LOG_UART_NO_GATE;

typedef struct log_info {
    const char *tag;
    esp_log_level_t level;
    size_t size;
    char *data;
} log_info_t;

/* Take a reference on g_log_config / g_log_queue; false when the module is
 * not initialized or is being de-initialized. */
static bool espnow_log_user_enter(void)
{
    if (!__atomic_load_n(&g_log_users_on, __ATOMIC_SEQ_CST)) {
        return false;   /**< not initialized: the common case costs one load */
    }

    __atomic_fetch_add(&g_log_users, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&g_log_users_on, __ATOMIC_SEQ_CST)) {
        return true;
    }

    __atomic_fetch_sub(&g_log_users, 1, __ATOMIC_SEQ_CST);
    return false;
}

static void espnow_log_user_exit(void)
{
    __atomic_fetch_sub(&g_log_users, 1, __ATOMIC_SEQ_CST);
}

esp_err_t espnow_log_get_config(espnow_log_config_t *config)
{
    ESP_PARAM_CHECK(config);
    ESP_ERROR_RETURN(!espnow_log_user_enter(), ESP_ERR_NOT_SUPPORTED, "log debugging is not initialized");

    memcpy(config, g_log_config, sizeof(espnow_log_config_t));
    espnow_log_user_exit();
    return ESP_OK;
}

esp_err_t espnow_log_set_config(const espnow_log_config_t *config)
{
    ESP_PARAM_CHECK(config);
    ESP_ERROR_RETURN(!espnow_log_user_enter(), ESP_ERR_NOT_SUPPORTED, "log debugging is not initialized");

    esp_err_t ret = ESP_OK;

    memcpy(g_log_config, config, sizeof(espnow_log_config_t));
    g_log_level_uart = (uint8_t)config->log_level_uart;
    espnow_log_user_exit();

    return ret;
}

static void espnow_log_send_task(void *arg)
{
    log_info_t *log_info = NULL;
    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame_head.security = CONFIG_ESPNOW_DEBUG_SECURITY;

    while (g_log_task_run) {
        if (xQueueReceive(g_log_queue, &log_info, pdMS_TO_TICKS(DEBUG_LOG_TIMEOUT_MS)) != pdPASS) {
            continue;
        }

        /* NULL: espnow_log_deinit() woke the task up, the loop condition ends it. */
        if (!log_info) {
            continue;
        }

        espnow_log_config_t *log_config = g_log_config;
        if (!log_config || !g_log_task_run) {
            /* Stopping: the line is dropped like those still in the queue. */
            DEBUG_LOG_FREE(log_info->data);
            DEBUG_LOG_FREE(log_info);
            continue;
        }

        if (log_info->level <= log_config->log_level_flash) {
            espnow_log_flash_write(log_info->data, log_info->size, log_info->level); /**< Write log data to flash */
        }

        if (log_info->level <= log_config->log_level_custom) {
            if (log_config->log_custom_write) {
                log_config->log_custom_write(log_info->data, log_info->size, log_info->tag, log_info->level); /**< Write log data to custom */
            }
        }

        /* Last, because it may cut the line: one ESP-NOW frame carries at most
         * ESPNOW_DATA_LEN bytes including the terminating NUL. The flash and
         * custom sinks above have been given the whole line and its real
         * length (log_info->size is left as it is), and the buffer is not
         * used again after the send. */
        if (strcasecmp(log_info->tag, "espnow") && log_info->level <= log_config->log_level_espnow) {
            size_t send_size = MIN(ESPNOW_DATA_LEN - 1, log_info->size) + 1;
            log_info->data[send_size - 1] = '\0';
            espnow_send(ESPNOW_DATA_TYPE_DEBUG_LOG, ESPNOW_ADDR_BROADCAST, log_info->data, send_size, &frame_head, portMAX_DELAY);
        }

        DEBUG_LOG_FREE(log_info->data);
        DEBUG_LOG_FREE(log_info);
    }

    /* Nothing of this module is used after this line: espnow_log_deinit() may
     * delete the queue and free the configuration as soon as it is set. */
    g_log_task_done = true;
    vTaskDelete(NULL);
}

void __real_esp_log_writev(esp_log_level_t level,
                           const char *tag,
                           const char *format,
                           va_list args);

/* Would ESP-IDF print this line? The wrappers see every ESP_LOGx that passed
 * the compile-time LOG_LOCAL_LEVEL, but esp_log_va() still drops it when the
 * tag's run-time level (esp_log_level_set(), console `log -t <tag> -l NONE`)
 * or, with Log V2, the master level is below `level`. The sinks must apply the
 * same decision, otherwise a tag silenced on the UART keeps producing ESP-NOW
 * traffic and flash writes. The sink levels are a second filter after this. */
static bool espnow_log_sdk_loggable(esp_log_level_t level, const char *tag)
{
    if (level == ESP_LOG_NONE) {
        return false;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#if ESP_LOG_VERSION == 2 && CONFIG_LOG_MASTER_LEVEL
    /* Log V1 checks the master level in the macro, before the wrapper is
     * reached; Log V2 checks it in esp_log(), which the wrapper replaces. */
    if (esp_log_get_level_master() < level) {
        return false;
    }
#endif
    /* esp_log_va(): V1 always, V2 unless constrained (the sinks skip
     * constrained contexts altogether). A NULL tag (Log V2: line printed
     * without a tag) has no level entry; esp_log_va() skips the check too. */
    return tag == NULL || esp_log_is_tag_loggable(level, tag);
#else
    /* esp_log_writev() applies the tag level itself before printing. */
    return tag == NULL || esp_log_level_get(tag) >= level;
#endif
}

/* Format one line and queue it for the flash / ESP-NOW / custom sinks. Consumes `args`.
 * `header`, when not NULL, is placed before the formatted text and a newline is
 * appended after it (Log V2 hands over the bare message; see __wrap_esp_log). */
static void espnow_log_capture_line(esp_log_level_t level, const char *tag, const char *header,
                                    const char *format, va_list args)
{
    const espnow_log_config_t *log_config = g_log_config;

    if (level > log_config->log_level_espnow
            && level > log_config->log_level_flash
            && level > log_config->log_level_custom) {
        return;
    }
    if (!espnow_log_sdk_loggable(level, tag)) {
        return;
    }

    /* Drop the line on OOM rather than crash inside the logging path. */
    log_info_t *log_info = DEBUG_LOG_MALLOC(sizeof(log_info_t));
    if (!log_info) {
        return;
    }

    /* A va_list may only be traversed once: measure on a copy, then format. */
    va_list size_args;
    va_copy(size_args, args);
    int body_len = vsnprintf(NULL, 0, format, size_args);
    va_end(size_args);
    if (body_len < 0) {
        DEBUG_LOG_FREE(log_info);
        return;
    }

    size_t header_len = header ? strlen(header) : 0;
    size_t total_len  = header_len + (size_t)body_len + (header ? 1 : 0);   /* + '\n' */
    log_info->data    = DEBUG_LOG_MALLOC(total_len + 1);
    if (!log_info->data) {
        DEBUG_LOG_FREE(log_info);
        return;
    }

    if (header_len) {
        memcpy(log_info->data, header, header_len);
    }
    vsnprintf(log_info->data + header_len, (size_t)body_len + 1, format, args);
    if (header) {
        log_info->data[header_len + body_len] = '\n';
    }
    log_info->data[total_len] = '\0';
    log_info->size  = total_len;
    log_info->level = level;
    /* esp_log() accepts a NULL tag ("not printed"); the send task compares and
     * forwards the tag, so store an empty string instead. The pointer is not
     * copied: like ESP-IDF, tags must be static constant strings. */
    log_info->tag   = tag ? tag : "";

    if (xQueueSend(g_log_queue, &log_info, 0) == pdFALSE) {
        DEBUG_LOG_FREE(log_info->data);
        DEBUG_LOG_FREE(log_info);
    }
}

/* Capture one line for the sinks, unless the module is not initialized or is
 * being de-initialized. The reference keeps g_log_config and g_log_queue alive
 * for the whole capture: espnow_log_deinit() waits for it. */
static void espnow_log_capture(esp_log_level_t level, const char *tag, const char *header,
                               const char *format, va_list args)
{
    if (!espnow_log_user_enter()) {
        return;
    }

    espnow_log_capture_line(level, tag, header, format, args);
    espnow_log_user_exit();
}

void __wrap_esp_log_writev(esp_log_level_t level,
                           const char *tag,
                           const char *format,
                           va_list args)
{
    /* g_log_level_uart, not g_log_config->log_level_uart: this runs without a
     * reference, so the configuration may be freed by espnow_log_deinit(). */
    if (level <= g_log_level_uart) {
        /* A va_list may only be traversed once. */
        va_list uart_args;
        va_copy(uart_args, args);
        __real_esp_log_writev(level, tag, format, uart_args);
        va_end(uart_args);
    }

    espnow_log_capture(level, tag, NULL, format, args);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
/* Task-context part of __wrap_esp_log(): UART gate, Log V2 text header and the
 * sink capture. Lives in flash; only called when the flash cache is on and the
 * scheduler runs (see the wrapper). Consumes `args`. */
static void __attribute__((noinline)) espnow_log_wrap_task(esp_log_config_t config, const char *tag,
                                                           const char *format, va_list args)
{
    esp_log_level_t level = config.opts.log_level;

    /* g_log_level_uart, not g_log_config->log_level_uart: this runs without a
     * reference, so the configuration may be freed by espnow_log_deinit(). */
    if (level <= g_log_level_uart) {
        va_list uart_args;
        va_copy(uart_args, args);
        esp_log_va(config, tag, format, uart_args);
        va_end(uart_args);
    }

    /* binary_mode: `format` is not a printable string. */
    if (config.opts.binary_mode) {
        return;
    }

    const char *header = NULL;
#if ESP_LOG_VERSION == 2
    /* With Log V1 `format` already carries the level, timestamp, tag and
     * newline. With Log V2 the macro hands over the bare message and
     * esp_log_format() adds them while printing, so rebuild the same text
     * header (without colour) for the sinks. */
    char header_buf[96];
    if (config.opts.require_formatting) {
        char timestamp[32] = "";
        if (!config.opts.dis_timestamp) {
            esp_log_timestamp_str(false, esp_log_timestamp64(false), timestamp);
        }
        snprintf(header_buf, sizeof(header_buf), "%c %s%s%s%s%s",
                 (level < ESP_LOG_MAX) ? "NEWIDV"[level] : '?',
                 config.opts.dis_timestamp ? "" : "(", timestamp, config.opts.dis_timestamp ? "" : ") ",
                 tag ? tag : "", tag ? ": " : "");
        header = header_buf;
    }
#endif
    espnow_log_capture(level, tag, header, format, args);
}

/* Since ESP-IDF v5.5 ESP_LOGx expands to esp_log(), which bypasses
 * esp_log_writev(); without this wrapper the sinks receive nothing.
 *
 * --wrap=esp_log redirects every esp_log() call in the image here, including
 * ESP_DRAM_LOGx / ESP_EARLY_LOGx (Log V2), which the SDK allows with the flash
 * cache disabled, from an ISR or before the scheduler starts. The SDK keeps
 * that path in IRAM (CONFIG_LOG_IN_IRAM: esp_log_va(),
 * esp_log_util_is_constrained(), the printing code), so this entry must be
 * in IRAM as well and, for a constrained call, may only read internal DRAM
 * (g_log_level_uart, not the heap configuration, which may be in external
 * RAM) and call the IRAM SDK path: no heap, no queue, no sink, nothing in
 * flash. Everything else is done by espnow_log_wrap_task() in flash. */
void IRAM_ATTR __wrap_esp_log(esp_log_config_t config, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    /* constrained_env is set at compile time by ESP_EARLY_LOGx / ESP_DRAM_LOGx;
     * with Log V2 a plain ESP_LOGx may also run from an ISR, with the cache
     * disabled or before the scheduler starts, which esp_log_va() detects at
     * run time with esp_log_util_is_constrained(). */
    bool constrained = config.opts.constrained_env;
#if ESP_LOG_VERSION == 2
    constrained = constrained || esp_log_util_is_constrained();
#endif
    if (constrained) {
        if (config.opts.log_level <= g_log_level_uart) {
            esp_log_va(config, tag, format, args);
        }
    } else {
        espnow_log_wrap_task(config, tag, format, args);
    }

    va_end(args);
}
#endif

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

    /* The wrappers capture only once g_log_users_on is set, which happens
       last. Until then every ESP_LOGx goes to the UART as without this module,
       and a failure below can free the queue and the configuration without
       any capture being in flight (the queue is still empty, so there is
       nothing to drain either): no task was ever able to call xQueueSend() on
       it. Bring the flash sink up first so a missing or too small partition
       (CONFIG_ESPNOW_DEBUG_LOG_FILE_MAX_SIZE) is reported to the caller. */
    if (config->log_level_flash != ESP_LOG_NONE) {
        ret = espnow_log_flash_init();
        if (ret != ESP_OK) {
            vQueueDelete(g_log_queue);
            g_log_queue = NULL;
            ESP_ERROR_RETURN(true, ret, "espnow_log_flash_init");
        }
    }

    espnow_log_config_t *log_config = ESP_CALLOC(1, sizeof(espnow_log_config_t));
    if (!log_config) {
        vQueueDelete(g_log_queue);
        g_log_queue = NULL;
        ESP_ERROR_RETURN(true, ESP_ERR_NO_MEM, "ESP_CALLOC g_log_config");
    }
    memcpy(log_config, config, sizeof(espnow_log_config_t));

    g_log_task_run  = true;
    g_log_task_done = false;
    if (xTaskCreatePinnedToCore(espnow_log_send_task, "espnow_log_send", 3 * 1024,
                                NULL, CONFIG_DEBUG_TASK_DEFAULT_PRIOTY,
                                &g_log_task, CONFIG_DEBUG_TASK_PINNED_TO_CORE) != pdPASS) {
        g_log_task_run  = false;
        g_log_task_done = true;
        g_log_task      = NULL;
        vQueueDelete(g_log_queue);
        g_log_queue = NULL;
        DEBUG_LOG_FREE(log_config);
        ESP_ERROR_RETURN(true, ESP_ERR_NO_MEM, "xTaskCreate espnow_log_send");
    }

    ESP_LOGI(TAG, "log initialized successfully");

    g_log_level_uart = (uint8_t)log_config->log_level_uart;
    g_log_config     = log_config;
    g_log_init_flag  = true;
    __atomic_store_n(&g_log_users_on, true, __ATOMIC_SEQ_CST);  /**< from here on the wrappers capture */

    return ESP_OK;
}

esp_err_t espnow_log_deinit()
{
    if (!g_log_init_flag) {
        return ESP_FAIL;
    }

    /* The send task cannot wait for itself (log_custom_write runs on it). */
    if (xTaskGetCurrentTaskHandle() == g_log_task) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 1. No new capture and no new espnow_log_get_config() / _set_config():
     *    from here on the wrappers only handle the UART (still with the
     *    configured level, until step 5). Then wait for the ones that are
     *    already inside (other tasks, the other core); they do not block, so
     *    this is a matter of ticks. */
    __atomic_store_n(&g_log_users_on, false, __ATOMIC_SEQ_CST);
    while (__atomic_load_n(&g_log_users, __ATOMIC_SEQ_CST)) {
        vTaskDelay(1);
    }

    /* 2. Stop the send task. Nothing is added to the queue any more; the NULL
     *    item wakes the task if it is waiting on an empty queue (if the queue
     *    is full the send fails, and the task is not waiting). It finishes
     *    the line it is delivering, drops what it receives after that and
     *    sets g_log_task_done as its last step. */
    log_info_t *log_info = NULL;
    g_log_task_run = false;
    xQueueSend(g_log_queue, &log_info, 0);
    while (!g_log_task_done) {
        vTaskDelay(1);
    }
    g_log_task = NULL;

    /* 3. Nobody uses the queue now: free the lines left in it and delete it. */
    while (xQueueReceive(g_log_queue, &log_info, 0) == pdPASS) {
        if (log_info) {
            DEBUG_LOG_FREE(log_info->data);
            DEBUG_LOG_FREE(log_info);
        }
    }
    vQueueDelete(g_log_queue);
    g_log_queue = NULL;

    /* 4. The flash sink (a no-op error when it was never brought up). */
    espnow_log_flash_deinit();

    /* 5. Back to the state before espnow_log_init(): no UART gate, no
     *    configuration; espnow_log_init() can be called again. */
    g_log_level_uart = ESPNOW_LOG_UART_NO_GATE;
    ESP_FREE(g_log_config);
    g_log_init_flag  = false;

    return ESP_OK;
}
