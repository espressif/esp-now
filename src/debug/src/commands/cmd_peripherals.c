// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
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

#include <string.h>

#include <sys/param.h>
#include "argtable3/argtable3.h"

#include "espnow.h"
#include "espnow_utils.h"

#include "esp_log.h"
#include "esp_console.h"

#include "driver/gpio.h"
#include "driver/uart.h"

static const char *TAG = "peripherals_cmd";

static struct {
    struct arg_int *config;
    struct arg_int *set;
    struct arg_int *get;
    struct arg_int *level;
    struct arg_end *end;
} gpio_args;

/**
 * @brief  A function which implements gpio command.
 */
static int gpio_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &gpio_args) != ESP_OK) {
        arg_print_errors(stderr, gpio_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (gpio_args.config->count) {
        uint32_t gpio_num = gpio_args.config->ival[0];
        esp_err_t ret = gpio_reset_pin(gpio_num);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "gpio_reset_pin, gpio_num: %d", gpio_num);
    }

    if (gpio_args.set->count && gpio_args.level->count) {
        uint32_t gpio_num = gpio_args.set->ival[0];

        gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
        gpio_set_level(gpio_num, gpio_args.level->ival[0]);
        ESP_LOGI(TAG, "Set gpio num: %d, level: %d", gpio_num, gpio_args.level->ival[0]);
    }

    if (gpio_args.get->count) {
        uint32_t gpio_num = gpio_args.get->ival[0];

        gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
        ESP_LOGI(TAG, "Get gpio num: %d, level: %d", gpio_num, gpio_get_level(gpio_num));
    }

    return ESP_OK;
}

/**
 * @brief  Register gpio command.
 */
static void register_gpio()
{
    gpio_args.config = arg_int0("c", "config", "<num>", "GPIO common configuration");
    gpio_args.get = arg_int0("g", "get", "<num>", "GPIO get input level");
    gpio_args.set = arg_int0("s", "set", "<num>", "GPIO set output level");
    gpio_args.level = arg_int0("l", "level", "<0 or 1>", "level. 0: low ; 1: high");
    gpio_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command  = "gpio",
        .help     = "GPIO common configuration",
        .hint     = NULL,
        .func     = &gpio_func,
        .argtable = &gpio_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_lit *start;
    struct arg_int *tx_io;
    struct arg_int *rx_io;
    struct arg_int *port_num;
    struct arg_int *baud_rate;
    struct arg_int *read;
    struct arg_str *write;
    struct arg_end *end;
} uart_args;

/**
 * @brief  A function which implements uart command.
 */
static int uart_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &uart_args) != ESP_OK) {
        arg_print_errors(stderr, uart_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret  = ESP_OK;
    static uart_port_t port_num = UART_NUM_0;


    if (uart_args.start->count) {
        uart_config_t uart_config = {
            .baud_rate = uart_args.baud_rate->count?uart_args.baud_rate->ival[0] : 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity    = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
        };

        int tx_io_num = UART_PIN_NO_CHANGE;
        int rx_io_num = UART_PIN_NO_CHANGE;

        if(uart_args.port_num->count) {
            port_num = uart_args.port_num->ival[0];
        }

        if (uart_args.tx_io->count) {
            tx_io_num = uart_args.tx_io->ival[0];
        }

        if (uart_args.rx_io->count) {
            rx_io_num = uart_args.rx_io->ival[0];
        }

        ESP_ERROR_CHECK(uart_param_config(port_num, &uart_config));
        ESP_ERROR_CHECK(uart_set_pin(port_num, tx_io_num, rx_io_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(port_num, 2 * ESPNOW_DATA_LEN, 2 * ESPNOW_DATA_LEN, 0, NULL, 0));
    } else if (uart_args.read->count) {
        uint8_t *data  = ESP_CALLOC(1, ESPNOW_DATA_LEN);
        ret = uart_read_bytes(port_num, data, ESPNOW_DATA_LEN, pdMS_TO_TICKS(uart_args.read->ival[0]));
        ESP_LOGI(TAG, "uart_read_bytes, size: %d, data: %s", ret, data);
        ESP_FREE(data);
        ESP_ERROR_RETURN(ret <= 0, ESP_FAIL, "uart_read_bytes, timeout: %d", uart_args.read->ival[0]);
    } else if (uart_args.write->count) {
        ret = uart_write_bytes(port_num, uart_args.write->sval[0], strlen(uart_args.write->sval[0]));
        ESP_ERROR_RETURN(ret <= 0, ESP_FAIL, "uart_write_bytes");
        ESP_LOGI(TAG, "uart_read_bytes, size: %d, data: %s", strlen(uart_args.write->sval[0]), uart_args.write->sval[0]);
    }

    return ESP_OK;
}

static void register_uart()
{
    uart_args.read = arg_int0("r", "read", "timeout_ms","UART read bytes from UART buffer");
    uart_args.write = arg_str0("w", "write", "data","UART write bytes from UART buffer");
    uart_args.start = arg_lit0("s", "start", "Install UART driver and set the UART to the default configuration");
    uart_args.tx_io = arg_int0(NULL, "tx_io", "<tx_io_num>", "UART TX pin GPIO number");
    uart_args.rx_io = arg_int0(NULL, "rx_io", "<rx_io_num>", "UART RX pin GPIO number");
    uart_args.baud_rate = arg_int0("b", "baud_rate", "<baud_rate>", "Set UART baud rate");
    uart_args.port_num = arg_int0("p", "port_num", "<0 | 1 | 2>", "Set UART port number");
    uart_args.end = arg_end(5);

    const esp_console_cmd_t cmd = {
        .command  = "uart",
        .help     = "uart common configuration",
        .hint     = NULL,
        .func     = &uart_func,
        .argtable = &uart_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void register_peripherals(void)
{
    register_gpio();
    register_uart();
}
