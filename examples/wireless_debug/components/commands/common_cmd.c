/* Common commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "esp_wifi.h"
#include "esp_utils.h"

#include "espnow.h"
#include "sdcard.h"
#include "debug_cmd.h"

#include "espnow_console.h"
#include "espnow_log.h"

#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_ctrl.h"

#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <errno.h>
#include <sys/param.h>

#define SDCARD_MOUNT_POINT "/sdcard"

static const char *TAG = "common_cmd";

static struct {
    struct arg_str *addr;
    struct arg_str *command;
    struct arg_lit *channel_all;
    struct arg_end *end;
} command_args;

/**
 * @brief  A function which implements `command` command.
 */
static int command_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &command_args) != ESP_OK) {
        arg_print_errors(stderr, command_args.end, argv[0]);
        return ESP_FAIL;
    }

    espnow_frame_head_t frame_head = {
        .filter_adjacent_channel = true,
    };

    esp_err_t ret = ESP_OK;
    size_t addrs_num = 0;
    espnow_addr_t *addr_list = ESP_MALLOC(sizeof(espnow_addr_t));

    for (const char *tmp = command_args.addr->sval[0];; tmp++) {
        if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
            mac_str2hex(tmp - 17, addr_list[addrs_num]);
            addrs_num++;

            if (*tmp == '\0' || *(tmp + 1) == '\0') {
                break;
            }

            addr_list = ESP_REALLOC(addr_list, sizeof(espnow_addr_t) * (addrs_num + 1));
        }
    }

    ESP_ERROR_RETURN(addrs_num <= 0, ESP_ERR_INVALID_ARG,
                     "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");

    if (command_args.channel_all->count) {
        frame_head.channel = ESPNOW_CHANNEL_ALL;
    }

    if (addrs_num == 1 && ESPNOW_ADDR_IS_BROADCAST(addr_list[0])) {
        frame_head.broadcast        = true;
        frame_head.retransmit_count = ESPNOW_RETRANSMIT_MAX_COUNT;
        frame_head.forward_rssi     = -25;
        frame_head.forward_ttl      = 1;

        ret = espnow_send(ESPNOW_TYPE_DEBUG_COMMAND, addr_list[0], command_args.command->sval[0], 
                    strlen(command_args.command->sval[0]) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
    } else if(addrs_num < 8) {
        frame_head.filter_adjacent_channel = true;

        for(int i = 0; i < addrs_num; ++i) {
            espnow_add_peer(addr_list[i], NULL);
            ret = espnow_send(ESPNOW_TYPE_DEBUG_COMMAND, addr_list[i], command_args.command->sval[0], 
                              strlen(command_args.command->sval[0]) + 1, &frame_head, portMAX_DELAY);
            ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
            espnow_del_peer(addr_list[i]);
        }
    } else {
        espnow_addr_t temp_group    = {0x0};
        frame_head.broadcast        = true;
        frame_head.retransmit_count = ESPNOW_RETRANSMIT_MAX_COUNT;
        frame_head.forward_rssi     = -25;
        frame_head.forward_ttl      = 1;

        esp_fill_random(temp_group, sizeof(espnow_addr_t));
        espnow_send_group(addr_list, addrs_num, temp_group, &frame_head, true, portMAX_DELAY);
        ret = espnow_send(ESPNOW_TYPE_DEBUG_COMMAND, temp_group, command_args.command->sval[0], 
                          strlen(command_args.command->sval[0]) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
        espnow_send_group(addr_list, addrs_num, temp_group, &frame_head, false, portMAX_DELAY);
    }

    ESP_FREE(addr_list);

    return ESP_OK;
}

/**
 * @brief  Register `command` command.
 */
void register_command()
{
    command_args.addr        = arg_str1(NULL, NULL, "<addr_list (xx:xx:xx:xx:xx:xx,xx:xx:xx:xx:xx:xx)>", "MAC of the monitored devices");
    command_args.command     = arg_str1(NULL, NULL, "<\"command\">", "Console command for the monitored device");
    command_args.channel_all = arg_lit0("a", "channel_all", "Send packets on all channels");
    command_args.end         = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "command",
        .help = "Let the console command run on the monitoring device",
        .hint = NULL,
        .func = &command_func,
        .argtable = &command_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_int *channel;
    struct arg_str *ssid;
    struct arg_str *bssid;
    struct arg_str *password;
    struct arg_str *country_code;
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
    }

    if (strlen((char *)wifi_config.sta.ssid)) {
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (wifi_config.sta.channel > 0 && wifi_config.sta.channel <= 14) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(wifi_config.sta.channel, WIFI_SECOND_CHAN_NONE));
        ESP_LOGI(TAG, "Set primary/secondary channel of ESP32, channel: %d", wifi_config.sta.channel);
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
        ESP_LOGI(TAG, "tx_power: %d, country: %s, channel: %d, mode: %d",
                 rx_power, country.cc, primary, mode);
    }

    return ret;
}

/**
 * @brief  Register wifi config command.
 */
void register_wifi_config()
{
    wifi_config_args.ssid     = arg_str0("s", "ssid", "<ssid>", "SSID of router");
    wifi_config_args.password = arg_str0("p", "password", "<password>", "Password of router");
    wifi_config_args.bssid    = arg_str0("b", "bssid", "<bssid (xx:xx:xx:xx:xx:xx)>", "BSSID of router");
    wifi_config_args.channel  = arg_int0("c", "channel", "<channel (1 ~ 13)>", "Channel of router");
    wifi_config_args.country_code  = arg_str0("C", "country_code", "<country_code ('CN', 'JP, 'US')>", "Set the current country code");
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
    struct arg_int *rssi;
    struct arg_str *ssid;
    struct arg_str *bssid;
    struct arg_int *passive;
    struct arg_end *end;
} wifi_scan_args;

/**
 * @brief  A function which implements wifi scan command.
 */
static esp_err_t wifi_scan_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &wifi_scan_args) != ESP_OK) {
        arg_print_errors(stderr, wifi_scan_args.end, argv[0]);
        return ESP_FAIL;
    }

    int8_t filter_rssi             = -120;
    uint16_t ap_number             = 0;
    uint8_t bssid[6]               = {0x0};
    uint8_t channel                = 1;
    wifi_second_chan_t second      = 0;
    wifi_ap_record_t *ap_record    = NULL;
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    ESP_ERROR_CHECK(esp_wifi_get_channel(&channel, &second));
    ESP_ERROR_CHECK(esp_wifi_disconnect());

    if (wifi_scan_args.passive->count) {
        scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        scan_config.scan_time.passive = wifi_scan_args.passive->ival[0];
    }

    if (wifi_scan_args.ssid->count) {
        scan_config.ssid = (uint8_t *)wifi_scan_args.ssid->sval[0];
    }

    if (wifi_scan_args.bssid->count) {
        ESP_ERROR_RETURN(!mac_str2hex(wifi_scan_args.bssid->sval[0], bssid), ESP_ERR_INVALID_ARG,
                         "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");
        scan_config.bssid = bssid;
    }

    if (wifi_scan_args.rssi->count) {
        filter_rssi = wifi_scan_args.rssi->ival[0];
        ESP_LOGW(TAG, "filter_rssi: %d", filter_rssi);
    }

    esp_wifi_scan_stop();

    int retry_count = 20;

    do {
        esp_wifi_disconnect();
        esp_wifi_scan_start(&scan_config, true);
        esp_wifi_scan_get_ap_num(&ap_number);
    } while (ap_number <= 0 && --retry_count);

    ESP_ERROR_RETURN(ap_number <= 0, ESP_FAIL, "esp_wifi_scan_get_ap_num");
    ESP_LOGI(TAG, "Get number of APs found, number: %d", ap_number);
    
    ap_record = ESP_MALLOC(sizeof(wifi_ap_record_t) * ap_number);
    ESP_ERROR_RETURN(ap_record == NULL, ESP_FAIL, "malloc ap_record");
    memset(ap_record, 0, sizeof(wifi_ap_record_t) * ap_number);

    int ret = esp_wifi_scan_get_ap_records(&ap_number, ap_record);
    if (ret != ESP_OK) {
        ESP_FREE(ap_record);
        ESP_ERROR_RETURN(1, ret, "esp_wifi_scan_get_ap_records");
    }

    for (int i = 0; i < ap_number; i++) {

        if (ap_record[i].rssi < filter_rssi) {
            continue;
        }

        ESP_LOGI(TAG, "Router, ssid: %s, bssid: " MACSTR ", channel: %u, rssi: %d",
                 ap_record[i].ssid, MAC2STR(ap_record[i].bssid),
                 ap_record[i].primary, ap_record[i].rssi);
    }


    if (channel > 0 && channel < 13) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(channel, second));
    }
    
    ESP_FREE(ap_record);

    return ESP_OK;
}

/**
 * @brief  Register wifi scan command.
 */
void register_wifi_scan()
{
    wifi_scan_args.rssi    = arg_int0("r", "rssi", "<rssi (-120 ~ 0)>", "Filter device uses RSSI");
    wifi_scan_args.ssid    = arg_str0("s", "ssid", "<ssid>", "Filter device uses SSID");
    wifi_scan_args.bssid   = arg_str0("b", "bssid", "<bssid (xx:xx:xx:xx:xx:xx)>", "Filter device uses AP's MAC");
    wifi_scan_args.passive = arg_int0("p", "passive", "<time (ms)>", "Passive scan time per channel");
    wifi_scan_args.end     = arg_end(5);

    const esp_console_cmd_t cmd = {
        .command  = "wifi_scan",
        .help     = "Wi-Fi is station mode, start scan ap",
        .hint     = NULL,
        .func     = &wifi_scan_func,
        .argtable = &wifi_scan_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

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

static struct {
    struct arg_str *addr;
    struct arg_lit *all;
    struct arg_int *rssi;
    struct arg_end *end;
} scan_args;

/**
 * @brief  A function which implements `find` find.
 */
static int scan_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &scan_args) != ESP_OK) {
        arg_print_errors(stderr, scan_args.end, argv[0]);
        return ESP_FAIL;
    }

    uint8_t addr[ESPNOW_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    char *data = "beacon";
    espnow_frame_head_t frame_head = {
        .retransmit_count = ESPNOW_RETRANSMIT_MAX_COUNT,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
    };

    if (scan_args.rssi->count) {
        frame_head.filter_weak_signal = true;
        frame_head.forward_rssi       = scan_args.rssi->ival[0];
    }

    if (scan_args.addr->count) {
        ESP_ERROR_RETURN(!mac_str2hex(scan_args.addr->sval[0], addr), ESP_ERR_INVALID_ARG,
                         "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");
    }

    if (scan_args.all->count) {
        uint8_t primary           = 0;
        wifi_second_chan_t second = 0;
        static wifi_country_t country = {0};

        esp_wifi_get_channel(&primary, &second);
        esp_wifi_get_country(&country);

        for (int i = 0; i < country.nchan; ++i) {
            esp_wifi_set_channel(country.schan + i, WIFI_SECOND_CHAN_NONE);
            frame_head.channel = country.schan + i;

            for(int count = 0; count < 3; ++count) {
                esp_err_t ret = espnow_send(ESPNOW_TYPE_DEBUG_COMMAND, addr,
                                            data, strlen(data) + 1, &frame_head, portMAX_DELAY);
                ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
                vTaskDelay(100);
            }

            /**< Waiting to receive the response message */
        }

        esp_wifi_set_channel(primary, second);
    } else {
        esp_err_t ret = espnow_send(ESPNOW_TYPE_DEBUG_COMMAND, addr,
                                    data, strlen(data) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
    }

    return ESP_OK;
}

void register_scan()
{
    scan_args.addr = arg_str0(NULL, NULL, "<addr (xx:xx:xx:xx:xx:xx)>", "MAC of the monitored device");
    scan_args.rssi = arg_int0("r", "rssi", "<rssi (-120 ~ 0)>", "Filter device uses RSSI");
    scan_args.all  = arg_lit0("a", "all", "Full channel scan");
    scan_args.end  = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "scan",
        .help = "Find devices that support ESP-NOW debug",
        .hint = NULL,
        .func = &scan_func,
        .argtable = &scan_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_int *beacon;
    struct arg_lit *add;
    struct arg_str *param;
    struct arg_end *end;
} prov_args;

/**
 * @brief  A function which implements `provisioning` provisioning.
 */
static int provisioning_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &prov_args) != ESP_OK) {
        arg_print_errors(stderr, prov_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    if (prov_args.beacon->count) {
        ESP_LOGI(TAG, "Find provisioning devices");

        espnow_prov_responder_t responder_info = {
            .product_id = "debug_board"
        };

        ret = espnow_prov_responder_beacon(&responder_info, pdMS_TO_TICKS(prov_args.beacon->ival[0]));
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_prov_responder_beacon");

        size_t num = 0;
        wifi_pkt_rx_ctrl_t rx_ctrl = {0};
        espnow_addr_t *initator_addr_list = NULL;
        espnow_prov_initator_t initator_info = {0};

        ESP_LOGI(TAG, "|        mac       | Channel | RSSI | Product Id | Device Name | Auth Mode |");
    
        for (int32_t start_ticks = xTaskGetTickCount(), recv_ticks = prov_args.beacon->ival[0]; recv_ticks > 0;
            recv_ticks = prov_args.beacon->ival[0] - (xTaskGetTickCount() - start_ticks)) {
            initator_addr_list = ESP_REALLOC(initator_addr_list, (num + 1) * sizeof(espnow_addr_t));
            ret = espnow_prov_responder_recv(initator_addr_list[num], &initator_info, &rx_ctrl, recv_ticks);

            ESP_ERROR_BREAK(ret == ESP_ERR_TIMEOUT, "");

            bool device_info_is_exist = false;

            for (int i = 0; i < num; ++i) {
                if (ESPNOW_ADDR_IS_EQUAL(initator_addr_list[num], initator_addr_list[i])) {
                    device_info_is_exist = true;
                    break;
                }
            }

            if (device_info_is_exist) {
                continue;
            }

            num++;

            ESP_LOGI(TAG, "| "MACSTR" | %d | %d | %12s | %15s | %d |",
                    MAC2STR(initator_addr_list[num]), rx_ctrl.channel, rx_ctrl.rssi,
                    initator_info.product_id, initator_info.device_name,
                    initator_info.auth_mode);
        }

        char (*addrs_str_list)[18] = ESP_MALLOC(num * 18 + 1);

        for (int i = 0; i < num; ++i) {
            sprintf(addrs_str_list[i], MACSTR "|", MAC2STR(initator_addr_list[i]));
        }

        ESP_LOGI(TAG, "responder, num: %d, list: %s", num, (char *)addrs_str_list);
        ESP_FREE(addrs_str_list);
        ESP_FREE(initator_addr_list);
    }

    if (prov_args.add->count) {
        ESP_LOGI(TAG, "Add device to the network: %s", prov_args.param->sval[0]);

        size_t addrs_num = 0;
        espnow_addr_t *initator_addr_list = ESP_MALLOC(sizeof(espnow_addr_t));

        for (const char *tmp = prov_args.param->sval[0];; tmp++) {
            if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
                mac_str2hex(tmp - 17, initator_addr_list[addrs_num]);
                addrs_num++;

                if (*tmp == '\0' || *(tmp + 1) == '\0') {
                    break;
                }

                initator_addr_list = ESP_REALLOC(initator_addr_list, sizeof(espnow_addr_t) * (addrs_num + 1));
            }
        }

        espnow_prov_wifi_t wifi_config = {0x0};
        strcpy((char *)wifi_config.sta.ssid, prov_args.param->sval[1]);
        strcpy((char *)wifi_config.sta.password, prov_args.param->sval[2]);
        ret = espnow_prov_responder_send(initator_addr_list, addrs_num, &wifi_config);
    
        ESP_FREE(initator_addr_list);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_prov_responder_add", esp_err_to_name(ret));

    }

    return ESP_OK;
}

void register_provisioning()
{
    prov_args.beacon = arg_int0("f", "find", "<wait_ticks>", "Find devices waiting to configure the network");
    prov_args.add   = arg_lit0("a", "add", "Configure network for devices");
    prov_args.param = arg_strn(NULL, NULL, "<addrs_list (xx:xx:xx:xx:xx:xx|xx:xx:xx:xx:xx:xx)> <ap_ssid> <app_password>",
                               0, 3, "Configure network for devices");
    prov_args.end   = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "provisioning",
        .help = "Configure network for devices",
        .hint = NULL,
        .func = &provisioning_func,
        .argtable = &prov_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_lit *list;
    struct arg_int *bind;
    struct arg_int *unbind;
    struct arg_int *command;
    struct arg_str *mac;
    struct arg_int *responder_attribute;
    struct arg_int *responder_value;
    struct arg_lit *ack;   
    struct arg_int *broadcast;  
    struct arg_lit *filter_weak_signal;   
    struct arg_lit *filter_adjacent_channel;  
    struct arg_int *forward_ttl;   
    struct arg_int *forward_rssi;
    struct arg_end *end;
} control_args;

/**
 * @brief  A function which implements `control` control.
 */
static int control_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &control_args) != ESP_OK) {
        arg_print_errors(stderr, control_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    espnow_frame_head_t frame_head = {    
        .retransmit_count = 10,
        .broadcast        = true,
        .channel          = ESPNOW_CHANNEL_ALL,
        .forward_ttl      = 10,
        .forward_rssi     = -25,
    };

    if (control_args.ack->count) {
        frame_head.ack = true;
    }

    if (control_args.broadcast->count) {
        frame_head.broadcast = control_args.broadcast->ival[0];
    }

    if (control_args.filter_weak_signal->count) {
        frame_head.filter_weak_signal = true;
    }

    if (control_args.filter_adjacent_channel->count) {
        frame_head.filter_adjacent_channel = true;
    }

    if (control_args.forward_ttl->count) {
        frame_head.forward_ttl = control_args.forward_ttl->ival[0];
    }

    if (control_args.forward_rssi->count) {
        frame_head.forward_rssi = control_args.forward_rssi->ival[0];
    }    

    if (control_args.command->count) {

        if(!control_args.responder_attribute->count || !control_args.responder_value->count) {
            ESP_LOGW(TAG, "Please enter the parameters: responder_attribute & responder_value");
            return ESP_ERR_INVALID_ARG;
        }

        espnow_ctrl_data_t data = {
            .initiator_attribute = control_args.command->ival[0],
            .responder_attribute = control_args.responder_attribute->ival[0],
            .responder_value_i   = control_args.responder_value->ival[0],
        };

        ESP_LOGI(TAG, "command, initiator_attribute: %d, responder_attribute: %d, responder_value: %d",
                  data.initiator_attribute, data.responder_attribute, data.responder_value_i);

        espnow_addr_t dest_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

        if (control_args.mac->count) {
            mac_str2hex(control_args.mac->sval[0], dest_addr);
        }

        ret = espnow_ctrl_send(dest_addr, &data, &frame_head, pdMS_TO_TICKS(1000));
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_ctrl_initiator_send");
    }

    if (control_args.bind->count) {
        ESP_LOGI(TAG, "The binding device, attribute: %d", control_args.bind->ival[0]);
        ret = espnow_ctrl_initiator_bind(control_args.bind->ival[0], true);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_ctrl_initiator_bind");
    }

    if (control_args.unbind->count) {
        ESP_LOGI(TAG, "The unbinding device, attribute: %d", control_args.unbind->ival[0]);
        ret = espnow_ctrl_initiator_bind(control_args.unbind->ival[0], false);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_ctrl_initiator_bind");
    }

    if (control_args.list->count) {
        size_t size = 0;
        espnow_ctrl_responder_get_bindlist(NULL, &size);

        if (size > 0) {
            espnow_ctrl_bind_info_t *list = ESP_CALLOC(size, sizeof(espnow_ctrl_bind_info_t));
            espnow_ctrl_responder_get_bindlist(list, &size);

            for (int i = 0; i < size; ++i) {
                ESP_LOGI("control_func", "mac: "MACSTR", initiator_type: %d, initiator_value: %d",
                         MAC2STR(list[i].mac), list[i].initiator_attribute >> 8, list[i].initiator_attribute & 0xff);
            }

            ESP_FREE(list);
        }

        return ESP_OK;
    }

    return ESP_OK;
}

void register_control()
{
    control_args.list    = arg_lit0("l", "list", "Get device binding list");
    control_args.bind    = arg_int0("b", "bind", "<initiator_attribute>", "Binding with response device");
    control_args.unbind  = arg_int0("u", "unbind", "<initiator_attribute>", "unBinding with response device");
    control_args.command = arg_int0("c", "command", "<initiator_attribute>", "Control command to bound device");
    control_args.responder_attribute = arg_int0("t", "responder_attribute", "<responder_attribute>", "Responder's attribute");
    control_args.responder_value     = arg_int0("v", "responder_value", "<responder_value>", "Responder's value");
    control_args.mac     = arg_str0("m", "mac", "<addr (xx:xx:xx:xx:xx:xx)>", "MAC of the monitored device");
    control_args.ack     = arg_lit0("a", "ack", "Wait for the receiving device to return ack to ensure transmission reliability");
    control_args.broadcast  = arg_int0("b", "broadcast", "<count>", "Data will not be forwarded until broadcast is enabled");
    control_args.filter_weak_signal      = arg_lit0("s", "filter_weak_signal", "When the signal received by the receiving device is lower than forward_rssi frame_head data will be discarded");
    control_args.filter_adjacent_channel = arg_lit0("C", "filter_adjacent_channel", "Because espnow is sent through HT20, it can receive packets from adjacent channels");
    control_args.forward_ttl  = arg_int0("t", "forward_ttl", "<count>", "Number of hops in data transfer");
    control_args.forward_rssi = arg_int0("r", "forward_rssi", "<rssi>", "When the data packet signal received by the receiving device is lower than forward_rssi");
    control_args.end          = arg_end(5);

    const esp_console_cmd_t cmd = {
        .command = "control",
        .help = "Control equipment by esp-now command",
        .hint = NULL,
        .func = &control_func,
        .argtable = &control_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
