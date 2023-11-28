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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_sleep.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "esp_chip_info.h"
#endif

#include "driver/rtc_io.h"
#include "driver/uart.h"

#include "mbedtls/base64.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "espnow_log.h"
#include "espnow_utils.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32C2
#include "esp32c2/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/spi_flash.h"
#elif CONFIG_IDF_TARGET_ESP32C6
#include "esp32c6/rom/spi_flash.h"
#endif

#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define WITH_TASKS_INFO 1
#endif

static const char *TAG = "system_cmd";

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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
        case CHIP_ESP32S3:
            chip_name = "ESP32S3";
            break;
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        case CHIP_ESP32C2:
            chip_name = "ESP32C2";
            break;
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
        case CHIP_ESP32C3:
            chip_name = "ESP32C3";
            break;
#endif

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
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    uint32_t raw_flash_id    = g_rom_flashchip.device_id;
#else
    uint32_t raw_flash_id    = rom_spiflash_legacy_data->chip.device_id;
#endif
    uint8_t mfg_id           = (raw_flash_id >> 16) & 0xFF;
    // uint32_t flash_id        = raw_flash_id & 0xFFFF;

    for (int i = 0; flash_chip[i].manufacturer; i++) {
        if (mfg_id == flash_chip[i].mfg_id) {
            flash_manufacturer = flash_chip[i].manufacturer;
            break;
        }
    }

    ESP_LOGI(__func__, "IDF Version:%s\r\n", esp_get_idf_version());
    ESP_LOGI(__func__, "chip_name: %s, chip_cores: %d, chip_revision: %d, flash_manufacturer: %s, flash_id: 0x%x, flash_size: %dMB, feature:%s%s%s%s",
             chip_name, chip_info.cores, chip_info.revision, flash_manufacturer, raw_flash_id, g_rom_flashchip.chip_size /1024/1024,
             chip_info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
             chip_info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
             chip_info.features & CHIP_FEATURE_BT ? "/BT" : "",
             chip_info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash" : "/External-Flash");

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
                 espnow_reboot_total_count(), reset_reason, espnow_reboot_is_exception(false));
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
 * @brief  A function which implements heap command.
 */
static int heap_func(int argc, char **argv)
{
    espnow_mem_print_record();
    espnow_mem_print_heap();
    espnow_mem_print_task();

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

/** 'tasks' command prints the list of tasks and related information */
#if WITH_TASKS_INFO

static int tasks_info(int argc, char **argv)
{
    const size_t bytes_per_task = 40; /* see vTaskList description */
    char *task_list_buffer = malloc(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (task_list_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate buffer for vTaskList output");
        return 1;
    }
    fputs("Task Name\tStatus\tPrio\tHWM\tTask#", stdout);
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
    fputs("\tAffinity", stdout);
#endif
    fputs("\n", stdout);
    vTaskList(task_list_buffer);
    fputs(task_list_buffer, stdout);
    free(task_list_buffer);
    return 0;
}

static void register_tasks(void)
{
    const esp_console_cmd_t cmd = {
        .command = "tasks",
        .help = "Get information about running tasks",
        .hint = NULL,
        .func = &tasks_info,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

#endif // WITH_TASKS_INFO

/** 'deep_sleep' command puts the chip into deep sleep mode */

static struct {
    struct arg_int *wakeup_time;
#if SOC_PM_SUPPORT_EXT_WAKEUP
    struct arg_int *wakeup_gpio_num;
    struct arg_int *wakeup_gpio_level;
#endif
    struct arg_end *end;
} deep_sleep_args;


static int deep_sleep(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &deep_sleep_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, deep_sleep_args.end, argv[0]);
        return 1;
    }
    if (deep_sleep_args.wakeup_time->count) {
        uint64_t timeout = 1000ULL * deep_sleep_args.wakeup_time->ival[0];
        ESP_LOGI(TAG, "Enabling timer wakeup, timeout=%lluus", timeout);
        ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(timeout) );
    }

#if SOC_PM_SUPPORT_EXT_WAKEUP
    if (deep_sleep_args.wakeup_gpio_num->count) {
        int io_num = deep_sleep_args.wakeup_gpio_num->ival[0];
        if (!esp_sleep_is_valid_wakeup_gpio(io_num)) {
            ESP_LOGE(TAG, "GPIO %d is not an RTC IO", io_num);
            return 1;
        }
        int level = 0;
        if (deep_sleep_args.wakeup_gpio_level->count) {
            level = deep_sleep_args.wakeup_gpio_level->ival[0];
            if (level != 0 && level != 1) {
                ESP_LOGE(TAG, "Invalid wakeup level: %d", level);
                return 1;
            }
        }
        ESP_LOGI(TAG, "Enabling wakeup on GPIO%d, wakeup on %s level",
                 io_num, level ? "HIGH" : "LOW");

        ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup(1ULL << io_num, level) );
        ESP_LOGE(TAG, "GPIO wakeup from deep sleep currently unsupported on ESP32-C3");
    }
#endif // SOC_PM_SUPPORT_EXT_WAKEUP

#if CONFIG_IDF_TARGET_ESP32
    rtc_gpio_isolate(GPIO_NUM_12);
#endif //CONFIG_IDF_TARGET_ESP32

    esp_deep_sleep_start();
    return 0;
}

static void register_deep_sleep(void)
{
    int num_args = 1;
    deep_sleep_args.wakeup_time =
        arg_int0("t", "time", "<t>", "Wake up time, ms");
#if SOC_PM_SUPPORT_EXT_WAKEUP
    deep_sleep_args.wakeup_gpio_num =
        arg_int0(NULL, "io", "<n>",
                 "If specified, wakeup using GPIO with given number");
    deep_sleep_args.wakeup_gpio_level =
        arg_int0(NULL, "io_level", "<0|1>", "GPIO level to trigger wakeup");
    num_args += 2;
#endif
    deep_sleep_args.end = arg_end(num_args);

    const esp_console_cmd_t cmd = {
        .command = "deep_sleep",
        .help = "Enter deep sleep mode. "
#if SOC_PM_SUPPORT_EXT_WAKEUP
        "Two wakeup modes are supported: timer and GPIO. "
#else
        "Timer wakeup mode is supported. "
#endif
        "If no wakeup option is specified, will sleep indefinitely.",
        .hint = NULL,
        .func = &deep_sleep,
        .argtable = &deep_sleep_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'light_sleep' command puts the chip into light sleep mode */

static struct {
    struct arg_int *wakeup_time;
    struct arg_int *wakeup_gpio_num;
    struct arg_int *wakeup_gpio_level;
    struct arg_end *end;
} light_sleep_args;

static int light_sleep(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &light_sleep_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, light_sleep_args.end, argv[0]);
        return 1;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (light_sleep_args.wakeup_time->count) {
        uint64_t timeout = 1000ULL * light_sleep_args.wakeup_time->ival[0];
        ESP_LOGI(TAG, "Enabling timer wakeup, timeout=%lluus", timeout);
        ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(timeout) );
    }
    int io_count = light_sleep_args.wakeup_gpio_num->count;
    if (io_count != light_sleep_args.wakeup_gpio_level->count) {
        ESP_LOGE(TAG, "Should have same number of 'io' and 'io_level' arguments");
        return 1;
    }
    for (int i = 0; i < io_count; ++i) {
        int io_num = light_sleep_args.wakeup_gpio_num->ival[i];
        int level = light_sleep_args.wakeup_gpio_level->ival[i];
        if (level != 0 && level != 1) {
            ESP_LOGE(TAG, "Invalid wakeup level: %d", level);
            return 1;
        }
        ESP_LOGI(TAG, "Enabling wakeup on GPIO%d, wakeup on %s level",
                 io_num, level ? "HIGH" : "LOW");

        ESP_ERROR_CHECK( gpio_wakeup_enable(io_num, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL) );
    }
    if (io_count > 0) {
        ESP_ERROR_CHECK( esp_sleep_enable_gpio_wakeup() );
    }
    if (CONFIG_ESP_CONSOLE_UART_NUM >= 0 && CONFIG_ESP_CONSOLE_UART_NUM <= UART_NUM_1) {
        ESP_LOGI(TAG, "Enabling UART wakeup (press ENTER to exit light sleep)");
        ESP_ERROR_CHECK( uart_set_wakeup_threshold(CONFIG_ESP_CONSOLE_UART_NUM, 3) );
        ESP_ERROR_CHECK( esp_sleep_enable_uart_wakeup(CONFIG_ESP_CONSOLE_UART_NUM) );
    }
    fflush(stdout);
    fsync(fileno(stdout));
    esp_light_sleep_start();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *cause_str;
    switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
        cause_str = "GPIO";
        break;
    case ESP_SLEEP_WAKEUP_UART:
        cause_str = "UART";
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        cause_str = "timer";
        break;
    default:
        cause_str = "unknown";
        printf("%d\n", cause);
    }
    ESP_LOGI(TAG, "Woke up from: %s", cause_str);
    return 0;
}

static void register_light_sleep(void)
{
    light_sleep_args.wakeup_time =
        arg_int0("t", "time", "<t>", "Wake up time, ms");
    light_sleep_args.wakeup_gpio_num =
        arg_intn(NULL, "io", "<n>", 0, 8,
                 "If specified, wakeup using GPIO with given number");
    light_sleep_args.wakeup_gpio_level =
        arg_intn(NULL, "io_level", "<0|1>", 0, 8, "GPIO level to trigger wakeup");
    light_sleep_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "light_sleep",
        .help = "Enter light sleep mode. "
        "Two wakeup modes are supported: timer and GPIO. "
        "Multiple GPIO pins can be specified using pairs of "
        "'io' and 'io_level' arguments. "
        "Will also wake up on UART input.",
        .hint = NULL,
        .func = &light_sleep,
        .argtable = &light_sleep_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
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
 * @brief  A function which implements rollback command.
 */
static int rollback_func(int argc, char **argv)
{
    esp_err_t err = ESP_OK;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);

    err = esp_ota_set_boot_partition(partition);
    ESP_ERROR_RETURN(err != ESP_OK, err, "esp_ota_set_boot_partition failed!");

    ESP_LOGI(__func__, "The next reboot will fall back to the previous version");

    return ESP_OK;
}

/**
 * @brief  Register rollback command.
 */
static void register_rollback()
{
    const esp_console_cmd_t cmd = {
        .command = "rollback",
        .help = "Upgrade error back to previous version",
        .hint = NULL,
        .func = &rollback_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_lit *length;
    struct arg_lit *output;
    struct arg_lit *erase;
    struct arg_end *end;
} coredump_args;

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
    struct arg_str *set;
    struct arg_lit *get;
    struct arg_str *time_zone;
    struct arg_end *end;
} time_args;

/**
 * @brief  A function which implements time command.
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
        sscanf(time_args.set->sval[0], "%lld", (long long int *)&tv_sec);

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
                 time_zone, strftime_buf, (long long)now.tv_sec);
    }

    return ESP_OK;
}

static void register_time(void)
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

void register_system(void)
{
    register_heap();
    register_version();
    register_restart();
#if WITH_TASKS_INFO
    register_tasks();
#endif

    register_deep_sleep();
    register_light_sleep();

    register_reset();
    register_rollback();
    register_coredump();
    register_time();
}
