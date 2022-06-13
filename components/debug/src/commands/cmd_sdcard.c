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

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "sdcard.h"

#include <sys/param.h>

static struct {
    struct arg_str *list;
    struct arg_str *remove;
    struct arg_str *output;
    struct arg_str *type;
    struct arg_end *end;
} sdcard_args;

/**
 * @brief  A function which implements sdcard command.
 */
static int sdcard_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &sdcard_args) != ESP_OK) {
        arg_print_errors(stderr, sdcard_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (sdcard_args.list->count) {
        sdcard_list_file(sdcard_args.list->sval[0]);
    }

    if (sdcard_args.remove->count) {
        sdcard_remove_file(sdcard_args.remove->sval[0]);
    }

    if (sdcard_args.output->count) {
        file_format_t type = FILE_TYPE_STRING;

        if (sdcard_args.type->count) {
            if (!strcasecmp(sdcard_args.type->sval[0], "string")) {
                type = FILE_TYPE_STRING;
            } else if (!strcasecmp(sdcard_args.type->sval[0], "hex")) {
                type = FILE_TYPE_HEX;
            } else if (!strcasecmp(sdcard_args.type->sval[0], "base64")) {
                type = FILE_TYPE_BASE64;
            }  else if (!strcasecmp(sdcard_args.type->sval[0], "bin")) {
                type = FILE_TYPE_BIN;
            } else {
                type = FILE_TYPE_NONE;
            }
        }

        sdcard_print_file(sdcard_args.output->sval[0], type, INT32_MAX);
    }

    return ESP_OK;
}

/**
 * @brief  Register sdcard command.
 */
void register_sdcard()
{
    sdcard_args.list   = arg_str0("l", "list", "<file_name>", "List all matched FILE(s)");
    sdcard_args.remove = arg_str0("r", "remove", "<file_name>", "Remove designation FILE(s)");
    sdcard_args.output = arg_str0("o", "output", "<file_name>", "Concatenate FILE(s) to standard output");
    sdcard_args.type   = arg_str0("t", "type", "<type (hex, string, base64)>", "FILE(s) output type");
    sdcard_args.end    = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "sdcard",
        .help = "SD-Card operation",
        .hint = NULL,
        .func = &sdcard_func,
        .argtable = &sdcard_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
