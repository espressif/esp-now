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

#include "sdcard.h"
#include "espnow_cmd.h"
#include "driver/gpio.h"

void espnow_console_commands_register(void)
{
    sdcard_config_t sdcard_config = {
        .gpio_num_cmd = GPIO_NUM_15,
        .gpio_num_d0  = GPIO_NUM_2,
        .gpio_num_d1  = GPIO_NUM_4,
        .gpio_num_d2  = GPIO_NUM_12,
        .gpio_num_d3  = GPIO_NUM_13,
    };

    /** Initializing SD card via SDMMC peripheral */
    sdcard_init(&sdcard_config);

    /** Register some commands */
    if (sdcard_is_mount()) {
        register_sdcard();
        register_wifi_sniffer();
    }

    register_espnow();
    register_system();
    register_wifi();
    register_peripherals();
    register_iperf();
}
