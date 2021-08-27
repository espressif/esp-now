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

#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "driver/uart.h"

#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#include "espnow_log.h"
#include "espnow.h"
#include "esp_spiffs.h"

#include "espnow_console.h"

#define PROMPT_STR CONFIG_IDF_TARGET

static bool g_running_flag = false;
static const char *TAG     = "espnow_console";

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
static char *g_store_history_filename = NULL;

static void initialize_filesystem(const espnow_console_config_t *config)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = config->store_history.base_path,
        .partition_label = config->store_history.partition_label,
        .max_files = 4,
        .format_if_mount_failed = true
    };

    ESP_LOGD(TAG, "base_path: %s", config->store_history.base_path);

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }

        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
}

static void initialize_console(void)
{
    /* Initialize the console */
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 512,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);

    /* Don't return empty lines */
    linenoiseAllowEmpty(false);
}

static void console_uart_handle_task(void *arg)
{
    const char *prompt = LOG_COLOR_I PROMPT_STR "> " LOG_RESET_COLOR;

#if CONFIG_LOG_COLORS
    /* Since the terminal doesn't support escape sequences,
     * don't use color codes in the prompt.
     */
    prompt = PROMPT_STR "> ";
#endif //CONFIG_LOG_COLORS

    while (g_running_flag) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char *line = linenoise(prompt);

        if (line == NULL) { /* Break on EOF or error */
            continue;
        }

        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);

            if (g_store_history_filename) {
                /* Save command history to filesystem */
                linenoiseHistorySave(g_store_history_filename);
            }
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);

        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Unrecognized command");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            ESP_LOGW(TAG, "Command returned non-zero error code: 0x%x (%s)", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "Internal error: %s", esp_err_to_name(err));
        }

        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    vTaskDelete(NULL);
}

static void console_espnow_handle_task(void *arg)
{
    esp_err_t ret       = ESP_OK;
    size_t        size = 0;
    char *data = ESP_MALLOC(ESPNOW_DATA_LEN);

    espnow_set_qsize(ESPNOW_TYPE_DEBUG_COMMAND, 16);

    extern wifi_pkt_rx_ctrl_t g_rx_ctrl;
    extern uint8_t g_src_addr[ESPNOW_ADDR_LEN];

    while (g_running_flag) {
        ret = espnow_recv(ESPNOW_TYPE_DEBUG_COMMAND, g_src_addr, data, &size, &g_rx_ctrl, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_broadcast, ret: %x, err_name: %s", ret, esp_err_to_name(ret));

        // printf("Debug: [%s, %d], data: %s\n", __func__, __LINE__, data);

        esp_err_t err = esp_console_run(data, &ret);

        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Unrecognized command");
        } else if (err == ESP_ERR_INVALID_ARG) {
            /**< Command was empty */
        } else if (err == ESP_OK && ret != ESP_OK) {
            ESP_LOGW(TAG, "Command returned non-zero error code: 0x%x (%s)", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "Internal error: %s", esp_err_to_name(err));
        }
    }

    espnow_set_qsize(ESPNOW_TYPE_DEBUG_COMMAND, 0);

    ESP_FREE(data);
    vTaskDelete(NULL);
}

esp_err_t espnow_console_init(const espnow_console_config_t *config)
{
    if (config->monitor_command.uart) {
        /* Drain stdout before reconfiguring it */
        fflush(stdout);
        fsync(fileno(stdout));

        /* Disable buffering on stdin */
        setvbuf(stdin, NULL, _IONBF, 0);

        /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
        esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
        /* Move the caret to the beginning of the next line on '\n' */
        esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

        /* Configure UART. Note that REF_TICK is used so that the baud rate remains
        * correct while APB frequency is changing in light sleep mode.
        */
        const uart_config_t uart_config = {
            .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
            .source_clk = UART_SCLK_REF_TICK,
#else
            .source_clk = UART_SCLK_XTAL,
#endif
        };
        /* Install UART driver for interrupt-driven reads and writes */
        ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,
                                            256, 0, 0, NULL, 0));
        ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));

        /* Tell VFS to use UART driver */
        esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    }

    initialize_console();

    if (config->store_history.base_path) {
        initialize_filesystem(config);
        /* Load command history from filesystem */
        asprintf(&g_store_history_filename, "%s/history.txt", config->store_history.base_path);

        linenoiseHistoryLoad(g_store_history_filename);
        ESP_LOGI(TAG, "Command history enabled");
    }

    /* Register commands */
    esp_console_register_help_command();

    g_running_flag = true;

    if (config->monitor_command.uart) {
        xTaskCreate(console_uart_handle_task, "console_uart", 1024 * 4, NULL, tskIDLE_PRIORITY + 1, NULL);
    }

    if (config->monitor_command.espnow) {
        xTaskCreate(console_espnow_handle_task, "console_espnow", 1024 * 4, NULL, tskIDLE_PRIORITY + 1, NULL);
    }

    return ESP_OK;
}

esp_err_t espnow_console_deinit()
{
    esp_err_t ret = ESP_OK;
    g_running_flag = false;

    ret = esp_console_deinit();
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "de-initialize console module");

    ESP_FREE(g_store_history_filename);

    return ESP_OK;
}
