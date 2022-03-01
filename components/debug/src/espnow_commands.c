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

#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "mbedtls/base64.h"

#include "esp_wifi.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_console.h"

#include "espnow.h"
#include "espnow_log.h"
#include "espnow_log_flash.h"
#include "espnow_console.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/spi_flash.h"
#endif

static const char *TAG = "debug_commands";

wifi_pkt_rx_ctrl_t g_rx_ctrl = {0};
uint8_t g_src_addr[ESPNOW_ADDR_LEN] = {0};
static struct {
    struct arg_lit *length;
    struct arg_lit *output;
    struct arg_lit *erase;
    struct arg_end *end;
} coredump_args;

static struct {
    struct arg_str *tag;
    struct arg_str *level;
    struct arg_str *mode;
    struct arg_str *flash;
    struct arg_lit *info;
    struct arg_end *end;
} log_args;


/**
 * @brief  A function which implements version command.
 */
static int version_func(int argc, char **argv)
{
    const char *chip_name = NULL;
    esp_chip_info_t chip_info = {0};

    /**< Pint system information */
    esp_chip_info(&chip_info);

    switch (chip_info.model) {
        case CHIP_ESP32:
            chip_name = "ESP32";
            break;

        case CHIP_ESP32S2:
            chip_name = "ESP32S2";
            break;

        case CHIP_ESP32S3:
            chip_name = "ESP32S3";
            break;

        case CHIP_ESP32C3:
            chip_name = "ESP32C3";
            break;

        default:
            chip_name = "Unknow";
            break;
    }

    struct flash_chip_info {
        const char *manufacturer;
        uint8_t mfg_id;
    } flash_chip[] = {
        { "MXIC",        0xC2},
        { "ISSI",        0x9D},
        { "WinBond",     0xEF},
        { "GD",          0xC8},
        { "XM25QU64A",   0x20},
        { NULL,          0xff},
    };

    const char *flash_manufacturer = "Unknow";
    uint32_t raw_flash_id    = g_rom_flashchip.device_id;
    uint8_t mfg_id           = (raw_flash_id >> 16) & 0xFF;
    // uint32_t flash_id        = raw_flash_id & 0xFFFF;

    for (int i = 0; flash_chip[i].manufacturer; i++) {
        if (mfg_id == flash_chip[i].mfg_id) {
            flash_manufacturer = flash_chip[i].manufacturer;
            break;
        }
    }

    ESP_LOGI(__func__, "chip_name: %s, chip_cores: %d, chip_revision: %d, flash_manufacturer: %s, flash_id: 0x%x, flash_size: %dMB",
             chip_name, chip_info.cores, chip_info.revision, flash_manufacturer, raw_flash_id, g_rom_flashchip.chip_size /1024/1024);

    return ESP_OK;
}

/**
 * @brief  Register version command.
 */
static void register_version()
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &version_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements log command.
 */
static int log_func(int argc, char **argv)
{
    const char *level_str[6] = {"NONE", "ERR", "WARN", "INFO", "DEBUG", "VER"};
    espnow_log_config_t log_config = {0};

    if (arg_parse(argc, argv, (void **)&log_args) != ESP_OK) {
        arg_print_errors(stderr, log_args.end, argv[0]);
        return ESP_FAIL;
    }

    espnow_log_get_config(&log_config);

    for (int log_level = 0; log_args.level->count && log_level < sizeof(level_str) / sizeof(char *); ++log_level) {
        if (!strcasecmp(level_str[log_level], log_args.level->sval[0])) {
            const char *tag = log_args.tag->count ? log_args.tag->sval[0] : "*";

            if (!log_args.mode->count) {
                esp_log_level_set(tag, log_level);
            } else {
                if (!strcasecmp(log_args.mode->sval[0], "flash")) {
                    log_config.log_level_flash = log_level;
                } else if (!strcasecmp(log_args.mode->sval[0], "uart")) {
                    log_config.log_level_uart = log_level;
                } else if (!strcasecmp(log_args.mode->sval[0], "espnow")) {
                    log_config.log_level_espnow = log_level;
                } else if (!strcasecmp(log_args.mode->sval[0], "custom")) {
                    log_config.log_level_custom = log_level;
                }
            }
        }
    }

    espnow_log_set_config(&log_config);

    if (log_args.info->count) { /**< Output enable type */
        ESP_LOGI(__func__, "log level, uart: %s, espnow: %s, flash: %s, custom: %s",
                 level_str[log_config.log_level_uart], level_str[log_config.log_level_espnow],
                 level_str[log_config.log_level_flash], level_str[log_config.log_level_custom]);
    }

    if (log_args.flash->count) {  /**< read to the flash of log data */
        int log_size = espnow_log_flash_size();

        if (!strcasecmp(log_args.flash->sval[0], "size")) {
            ESP_LOGI(__func__, "The flash partition that stores the log size: %d", log_size);
        } else if (!strcasecmp(log_args.flash->sval[0], "data")) {
            char *log_data = ESP_MALLOC(ESPNOW_DATA_LEN);

            for (size_t size = MIN(ESPNOW_DATA_LEN, log_size);
                    size > 0 && espnow_log_flash_read(log_data, &size) == ESP_OK;
                    log_size -= size, size = MIN(ESPNOW_DATA_LEN, log_size)) {
                printf("%.*s", size, log_data);
                fflush(stdout);
            }

            ESP_FREE(log_data);
        } else if (!strcasecmp(log_args.flash->sval[0], "espnow")) {
            char *log_data = ESP_MALLOC(ESPNOW_DATA_LEN);

            for (size_t size = MIN(ESPNOW_DATA_LEN, log_size);
                    size > 0 && espnow_log_flash_read(log_data, &size) == ESP_OK;
                    log_size -= size, size = MIN(ESPNOW_DATA_LEN, log_size)) {

                if (size < ESPNOW_DATA_LEN) {
                    log_data[size] = '\0';
                    size++;
                }

                espnow_send(ESPNOW_TYPE_DEBUG_LOG, ESPNOW_ADDR_BROADCAST,
                            log_data, size, NULL, portMAX_DELAY);
            }

            ESP_FREE(log_data);
        } else {
            ESP_LOGE(__func__, "Parameter error, please input: 'size', 'data' or 'espnow'");
        }

    }

    return ESP_OK;
}

/**
 * @brief  Register log command.
 */
static void register_log()
{
    log_args.tag   = arg_str0("t", "tag", "<tag>", "Tag of the log entries to enable, '*' resets log level for all tags to the given value");
    log_args.level = arg_str0("l", "level", "<level>", "Selects log level to enable (NONE, ERR, WARN, INFO, DEBUG, VER)");
    log_args.mode  = arg_str0("m", "mode", "<mode('uart', 'flash', 'espnow' or 'custom')>", "Selects log to mode ('uart', 'flash', 'espnow' or 'custom')");
    log_args.flash = arg_str0("f", "flash", "<operation ('size', 'data' or 'espnow')>", "Read to the flash of log information");
    log_args.info  = arg_lit0("i", "info", "Configuration of output log");
    log_args.end   = arg_end(8);

    const esp_console_cmd_t cmd = {
        .command = "log",
        .help = "Set log level for given tag",
        .hint = NULL,
        .func = &log_func,
        .argtable = &log_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_lit *info;
    struct arg_end *end;
} restart_args;

/**
 * @brief  A function which implements `restart`
 */
static int restart_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &restart_args) != ESP_OK) {
        arg_print_errors(stderr, restart_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (restart_args.info->count) {
        esp_reset_reason_t reset_reason = esp_reset_reason();
        ESP_LOGI(__func__, "num: %d, reason: %d, crash: %d",
                 esp_reboot_total_count(), reset_reason, esp_reboot_is_exception(false));
    } else {
        ESP_LOGI(__func__, "Restarting");
        esp_restart();
    }

    return ESP_OK;
}

/**
 * @brief  Register restart command.
 */
static void register_restart()
{
    restart_args.info = arg_lit0("i", "info", "Get restart information");
    restart_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command  = "restart",
        .help     = "Reset of the chip",
        .hint     = NULL,
        .func     = &restart_func,
        .argtable = &restart_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements reset command.
 */
static int reset_func(int argc, char **argv)
{
    esp_err_t ret = ESP_OK;
    esp_partition_iterator_t part_itra = NULL;
    const esp_partition_t *nvs_part = NULL;

    ESP_LOGI(__func__, "Erase part of the nvs partition");

    part_itra = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "nvs");
    ESP_ERROR_RETURN(!part_itra, ESP_ERR_NOT_SUPPORTED, "partition no find, subtype: 0x%x, label: %s",
                     ESP_PARTITION_SUBTYPE_ANY, "nvs");

    nvs_part = esp_partition_get(part_itra);
    ESP_ERROR_RETURN(!nvs_part, ESP_ERR_NOT_SUPPORTED, "esp_partition_get");

    ret = esp_partition_erase_range(nvs_part, 0, nvs_part->size);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "Erase part of the nvs partition");

    ESP_LOGI(__func__, "Restarting");
    esp_restart();
}

/**
 * @brief  Register reset command.
 */
static void register_reset()
{
    const esp_console_cmd_t cmd = {
        .command = "reset",
        .help = "Clear device configuration information",
        .hint = NULL,
        .func = &reset_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements reset command.
 */
static int fallback_func(int argc, char **argv)
{
    esp_err_t err = ESP_OK;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);

    err = esp_ota_set_boot_partition(partition);
    ESP_ERROR_RETURN(err != ESP_OK, err, "esp_ota_set_boot_partition failed!");

    ESP_LOGI(__func__, "The next reboot will fall back to the previous version");

    return ESP_OK;
}

/**
 * @brief  Register fallback command.
 */
static void register_fallback()
{
    const esp_console_cmd_t cmd = {
        .command = "fallback",
        .help = "Upgrade error back to previous version",
        .hint = NULL,
        .func = &fallback_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements heap command.
 */
static int heap_func(int argc, char **argv)
{
    esp_mem_print_record();
    esp_mem_print_heap();
    esp_mem_print_task();

    if (!heap_caps_check_integrity_all(true)) {
        ESP_LOGE(__func__, "At least one heap is corrupt");
    }

    return ESP_OK;
}

/**
 * @brief  Register heap command.
 */
static void register_heap()
{
    const esp_console_cmd_t cmd = {
        .command = "heap",
        .help = "Get the current size of free heap memory",
        .hint = NULL,
        .func = &heap_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements heap command.
 */
static int beacon_func(int argc, char **argv)
{
    esp_err_t ret = ESP_OK;
    char *beacon_data = NULL;
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();

    espnow_add_peer(g_src_addr, NULL);

    asprintf(&beacon_data, LOG_COLOR_I "I (%u) %s: project_name: %s, app_version: %s, esp-idf_version: %s, "
             "free_heap: %d, total_heap: %d, rx_rssi: %d, compile_time: %s %s\n",
             esp_log_timestamp(), __func__,
             app_desc->project_name, app_desc->version, app_desc->idf_ver,
             esp_get_free_heap_size(), heap_caps_get_total_size(MALLOC_CAP_DEFAULT), g_rx_ctrl.rssi,
             app_desc->date, app_desc->time);
    ret = espnow_send(ESPNOW_TYPE_DEBUG_LOG, g_src_addr,
                      beacon_data, strlen(beacon_data) + 1, NULL, portMAX_DELAY);

    espnow_del_peer(g_src_addr);
    free(beacon_data);
    return ret;
}

/**
 * @brief  Register heap command.
 */
static void register_espnow_beacon()
{
    const esp_console_cmd_t cmd = {
        .command = "beacon",
        .help = "Send ESP-NOW broadcast to let other devices discover",
        .hint = NULL,
        .func = &beacon_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements coredump command.
 */
static int coredump_func(int argc, char **argv)
{
    esp_err_t ret         = ESP_OK;
    ssize_t coredump_size = 0;
    const esp_partition_t *coredump_part = NULL;

    if (arg_parse(argc, argv, (void **)&coredump_args) != ESP_OK) {
        arg_print_errors(stderr, coredump_args.end, argv[0]);
        return ESP_FAIL;
    }

    coredump_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                    ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    ESP_ERROR_RETURN(coredump_part == NULL, ESP_ERR_NOT_SUPPORTED, "No core dump partition found!");

    ret = esp_partition_read(coredump_part, 4, &coredump_size, sizeof(size_t));
    ESP_ERROR_RETURN(coredump_part == NULL, ESP_ERR_NOT_SUPPORTED, "Core dump read length!");

    if (coredump_args.length->count) {
        ESP_LOGI(__func__, "Core dump is length: %d Bytes", coredump_size);
    }

    if (coredump_args.output->count && coredump_size > 0) {
#define COREDUMP_BUFFER_SIZE 150
        uint8_t *buffer = ESP_REALLOC_RETRY(NULL, COREDUMP_BUFFER_SIZE);
        ESP_LOGI(__func__, "\n================= CORE DUMP START =================\n");

        for (int offset = 4; offset < coredump_size; offset += COREDUMP_BUFFER_SIZE) {
            size_t size = MIN(COREDUMP_BUFFER_SIZE, coredump_size - offset);
            esp_partition_read(coredump_part, offset, buffer, size);
            size_t dlen = (size + 2) / 3 * 4; //base64 encode maximum length = ⌈ n / 3 ⌉ * 4
            size_t olen = 0;
            uint8_t *b64_buf = ESP_MALLOC(dlen);
            mbedtls_base64_encode(b64_buf, dlen, &olen, buffer, size);
            ESP_LOGI(__func__, "%s", b64_buf);
            ESP_FREE(b64_buf);
        }

        ESP_LOGI(__func__, "================= CORE DUMP END ===================\n");

        ESP_LOGI(__func__, "1. Save core dump text body to some file manually");
        ESP_LOGI(__func__, "2. Run the following command: \n"
                 "python esp-idf/components/espcoredump/espcoredump.py info_corefile -t b64 -c </path/to/saved/base64/text> </path/to/program/elf/file>");
        ESP_FREE(buffer);
    }

    if (coredump_args.erase->count) {
        ret = esp_partition_erase_range(coredump_part, 0, coredump_part->size);
        ESP_ERROR_RETURN(ret != ESP_OK, ESP_FAIL, "Core dump erase fail");
        ESP_LOGI(__func__, "Core dump erase successful");
    }

    return ESP_OK;
}

/**
 * @brief  Register coredump command.
 */
static void register_coredump()
{
    coredump_args.length = arg_lit0("l", "length", "Get coredump data length");
    coredump_args.output = arg_lit0("o", "output", "Read the coredump data of the device");
    coredump_args.erase  = arg_lit0("e", "erase", "Erase the coredump data of the device");
    coredump_args.end    = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "coredump",
        .help = "Get core dump information",
        .hint = NULL,
        .func = &coredump_func,
        .argtable = &coredump_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_str *country_code;
    struct arg_int *channel;
    struct arg_str *ssid;
    struct arg_str *bssid;
    struct arg_str *password;
    struct arg_int *tx_power;
    struct arg_lit *info;
    struct arg_end *end;
} wifi_config_args;

/**
 * @brief  A function which implements wifi config command.
 */
static int wifi_config_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &wifi_config_args) != ESP_OK) {
        arg_print_errors(stderr, wifi_config_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    wifi_config_t wifi_config = {0x0};

    if (wifi_config_args.ssid->count) {
        strcpy((char *)wifi_config.sta.ssid, wifi_config_args.ssid->sval[0]);
    }

    if (wifi_config_args.password->count) {
        strcpy((char *)wifi_config.sta.password, wifi_config_args.password->sval[0]);
    }

    if (wifi_config_args.bssid->count) {
        ESP_ERROR_RETURN(!mac_str2hex(wifi_config_args.bssid->sval[0], wifi_config.sta.bssid), ESP_ERR_INVALID_ARG,
                         "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");
    }

    if (strlen((char *)wifi_config.sta.ssid)) {
        ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_wifi_set_config");

        ret = esp_wifi_connect();
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_wifi_set_config");
    }

    if (wifi_config_args.country_code->count) {
        wifi_country_t country = {0};
        const char *country_code = wifi_config_args.country_code->sval[0];

        if (!strcasecmp(country_code, "US")) {
            strcpy(country.cc, "US");
            country.schan = 1;
            country.nchan = 11;
        } else if (!strcasecmp(country_code, "JP")) {
            strcpy(country.cc, "JP");
            country.schan = 1;
            country.nchan = 14;
        }  else if (!strcasecmp(country_code, "CN")) {
            strcpy(country.cc, "CN");
            country.schan = 1;
            country.nchan = 13;
        } else {
            return ESP_ERR_INVALID_ARG;
        }

        ret = esp_wifi_set_country(&country);
    }

    if (wifi_config_args.channel->count) {
        wifi_config.sta.channel = wifi_config_args.channel->ival[0];

        if (wifi_config.sta.channel > 0 && wifi_config.sta.channel <= 14) {
            ret = esp_wifi_set_channel(wifi_config.sta.channel, WIFI_SECOND_CHAN_NONE);
            ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_wifi_set_channel");
            ESP_LOGI(__func__, "Set Channel, channel: %d", wifi_config.sta.channel);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    }

    if (wifi_config_args.tx_power->count) {
        esp_wifi_set_max_tx_power(wifi_config_args.tx_power->ival[0]);
    }

    if (wifi_config_args.info->count) {
        int8_t rx_power           = 0;
        wifi_country_t country    = {0};
        uint8_t primary           = 0;
        wifi_second_chan_t second = 0;
        wifi_mode_t mode          = 0;

        esp_wifi_get_channel(&primary, &second);
        esp_wifi_get_max_tx_power(&rx_power);
        esp_wifi_get_country(&country);
        esp_wifi_get_mode(&mode);

        country.cc[2] = '\0';
        ESP_LOGI(__func__, "rx: %d, tx_power: %d, country: %s, channel: %d, mode: %d",
                 g_rx_ctrl.rssi, rx_power, country.cc, primary, mode);
    }

    return ret;
}

/**
 * @brief  Register wifi config command.
 */
static void register_wifi_config()
{
    wifi_config_args.ssid     = arg_str0("s", "ssid", "<ssid>", "SSID of router");
    wifi_config_args.password = arg_str0("p", "password", "<password>", "Password of router");
    wifi_config_args.bssid    = arg_str0("b", "bssid", "<bssid (xx:xx:xx:xx:xx:xx)>", "BSSID of router");
    wifi_config_args.channel  = arg_int0("c", "channel", "<channel (1 ~ 14)>", "Set primary channel");
    wifi_config_args.country_code  = arg_str0("C", "country_code", "<country_code ('CN', 'JP, 'US')>", "Set the current country code");
    wifi_config_args.tx_power = arg_int0("t", "tx_power", "<power (8 ~ 84)>", "Set maximum transmitting power after WiFi start.");
    wifi_config_args.info     = arg_lit0("i", "info", "Get Wi-Fi configuration information");
    wifi_config_args.end      = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "wifi_config",
        .help = "Set the configuration of the ESP32 STA",
        .hint = NULL,
        .func = &wifi_config_func,
        .argtable = &wifi_config_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_int *config;
    struct arg_int *set;
    struct arg_int *get;
    struct arg_int *level;
    struct arg_end *end;
} gpio_args;

/**
 * @brief  A function which implements `gpio`
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
        uint32_t gpio_num = gpio_args.set->ival[0];

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
 * @brief  A function which implements `gpio`
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


static struct {
    struct arg_str *set;
    struct arg_lit *get;
    struct arg_str *time_zone;
    struct arg_end *end;
} time_args;

/**
 * @brief  A function which implements `gpio`
 */
static int time_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &time_args) != ESP_OK) {
        arg_print_errors(stderr, time_args.end, argv[0]);
        return ESP_FAIL;
    }

    const char *time_zone = "CST-8";

    if (time_args.time_zone->count) {
        time_zone = time_args.time_zone->sval[0];
    }

    if (time_args.set->count) {
        setenv("TZ", time_zone, 1);
        tzset();
        uint64_t tv_sec = 0;
        sscanf(time_args.set->sval[0], "%lld", &tv_sec);

        struct timeval now = { .tv_sec = tv_sec};
        settimeofday(&now, NULL);
    }

    if (time_args.get->count) {
        struct timeval now    = {0};
        struct tm timeinfo    = {0};
        char strftime_buf[64] = {0};

        setenv("TZ", time_zone, 1);
        tzset();
        gettimeofday(&now, NULL);
        localtime_r(&now.tv_sec, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "time_zone: %s, strftime: %s, sec: %lld", 
                 time_zone, strftime_buf, now.tv_sec);
    }

    return ESP_OK;
}

static void register_time()
{
    time_args.get = arg_lit0("g", "get", "Get system time");
    time_args.set = arg_str0("s", "set", "<utc>", "Set system time");
    time_args.time_zone = arg_str0("z", "time_zone", "<time_zone>", "Time zone");
    time_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command  = "time",
        .help     = "time common configuration",
        .hint     = NULL,
        .func     = &time_func,
        .argtable = &time_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void espnow_console_commands_register()
{
    register_version();
    register_heap();
    register_restart();
    register_reset();
    register_time();
    register_log();
    register_espnow_beacon();
    register_fallback();
    register_coredump();
    register_wifi_config();

    register_gpio();
    register_uart();
}
