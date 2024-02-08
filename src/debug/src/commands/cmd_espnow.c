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

#include "esp_wifi.h"


#include "espnow.h"
#include "espnow_console.h"
#include "espnow_log.h"
#include "espnow_utils.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "esp_timer.h"

#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_ctrl.h"
#include "espnow_security.h"
#include "espnow_security_handshake.h"

#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <errno.h>
#include <sys/param.h>

static const char *TAG = "espnow_cmd";

wifi_pkt_rx_ctrl_t g_rx_ctrl = {0};
uint8_t g_src_addr[ESPNOW_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0XFF};

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
        .security                = CONFIG_ESPNOW_DEBUG_SECURITY,
    };

    esp_err_t ret = ESP_OK;
    size_t addrs_num = 0;
    espnow_addr_t *addr_list = ESP_MALLOC(sizeof(espnow_addr_t));

    for (const char *tmp = command_args.addr->sval[0];; tmp++) {
        if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
            espnow_mac_str2hex(tmp - 17, addr_list[addrs_num]);
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
        frame_head.filter_adjacent_channel = false;
    }

    if (addrs_num == 1 && ESPNOW_ADDR_IS_BROADCAST(addr_list[0])) {
        frame_head.broadcast        = true;
        frame_head.retransmit_count = ESPNOW_RETRANSMIT_MAX_COUNT;
        frame_head.forward_rssi     = -25;
        frame_head.forward_ttl      = 1;

        ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_COMMAND, addr_list[0], command_args.command->sval[0], 
                    strlen(command_args.command->sval[0]) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
    } else if(addrs_num < 8) {
        for(int i = 0; i < addrs_num; ++i) {
            espnow_add_peer(addr_list[i], NULL);
            ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_COMMAND, addr_list[i], command_args.command->sval[0], 
                              strlen(command_args.command->sval[0]) + 1, &frame_head, portMAX_DELAY);
            ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
            espnow_del_peer(addr_list[i]);
        }
    } else {
        espnow_addr_t temp_group    = {0x0};
        frame_head.group            = 1;
        frame_head.broadcast        = true;
        frame_head.retransmit_count = ESPNOW_RETRANSMIT_MAX_COUNT;
        frame_head.forward_rssi     = -25;
        frame_head.forward_ttl      = 1;

        esp_fill_random(temp_group, sizeof(espnow_addr_t));
        espnow_set_group(addr_list, addrs_num, temp_group, &frame_head, true, portMAX_DELAY);
        ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_COMMAND, temp_group, command_args.command->sval[0], 
                          strlen(command_args.command->sval[0]) + 1, &frame_head, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
        espnow_set_group(addr_list, addrs_num, temp_group, &frame_head, false, portMAX_DELAY);
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

    const char *data = "beacon";
    espnow_frame_head_t frame_head = {
        .retransmit_count = ESPNOW_RETRANSMIT_MAX_COUNT,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
        .security                = CONFIG_ESPNOW_DEBUG_SECURITY,
    };

    if (scan_args.rssi->count) {
        frame_head.filter_weak_signal = true;
        frame_head.forward_rssi       = scan_args.rssi->ival[0];
    }

    if (scan_args.addr->count) {
        ESP_ERROR_RETURN(!espnow_mac_str2hex(scan_args.addr->sval[0], addr), ESP_ERR_INVALID_ARG,
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
                esp_err_t ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_COMMAND, addr,
                                            data, strlen(data) + 1, &frame_head, portMAX_DELAY);
                ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
                vTaskDelay(100);
            }

            /**< Waiting to receive the response message */
        }

        esp_wifi_set_channel(primary, second);
    } else {
        esp_err_t ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_COMMAND, addr,
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
    struct arg_lit *erase;
    struct arg_lit *responder;
    struct arg_int *initiator;
    struct arg_str *param;
    struct arg_end *end;
} prov_args;

static int s_device_num;
static esp_err_t responder_recv_callback(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_prov_initiator_t *initiator_info = (espnow_prov_initiator_t *)data;
    /**
     * @brief Authenticate the device through the information of the initiator
     */
    s_device_num ++;
    ESP_LOGI(TAG, "NUM: %d, MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s, Auth Mode: %d, device_secret: %s",
                s_device_num, MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi,
                initiator_info->product_id, initiator_info->device_name,
                initiator_info->auth_mode, initiator_info->device_secret);

    return ESP_OK;
}

static TaskHandle_t s_prov_task;

static esp_err_t initiator_recv_callback(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_prov_wifi_t *wifi_config = (espnow_prov_wifi_t *)data;
    wifi_config_t wifi_sta_config = { 0 };
    memcpy(&wifi_sta_config, &wifi_config->sta, sizeof(wifi_config->sta));
    ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, wifi_mode: %d, ssid: %s, password: %s, token: %s",
                MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi,
                wifi_config->mode, wifi_config->sta.ssid, wifi_config->sta.password, wifi_config->token);

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_sta_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
    return ESP_OK;
}

static void provisioning_initiator(void *arg)
{
    esp_err_t ret = ESP_OK;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    espnow_prov_initiator_t initiator_info = {
        .product_id = "debug_board",
    };
    espnow_addr_t responder_addr = {0};
    espnow_prov_responder_t responder_info = {0};

    for (;;) {
        ret = espnow_prov_initiator_scan(responder_addr, &responder_info, &rx_ctrl, pdMS_TO_TICKS(3 * 1000));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "");

        ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s",
                 MAC2STR(responder_addr), rx_ctrl.channel, rx_ctrl.rssi,
                 responder_info.product_id, responder_info.device_name);

        ret = espnow_prov_initiator_send(responder_addr, &initiator_info, initiator_recv_callback, pdMS_TO_TICKS(3 * 1000));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_prov_responder_add", esp_err_to_name(ret));

        break;
    }

    ESP_LOGI(TAG, "provisioning initiator exit");
    vTaskDelete(NULL);
    s_prov_task = NULL;
}

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

    if (prov_args.erase->count) {
        esp_wifi_restore();
        esp_wifi_disconnect();
        esp_restart();
    }

    if (prov_args.responder->count) {
        if (!s_prov_task) {
            xTaskCreate(provisioning_initiator, "PROV_init", 3072, NULL, tskIDLE_PRIORITY + 1, &s_prov_task);
            ESP_LOGI(TAG, "Start provisioning");
        } else {
            ESP_LOGI(TAG, "Already start provisioning");
        }
    }

    if (prov_args.initiator->count) {

        if (!prov_args.param->count) {
            ESP_LOGW(TAG, "Please set wifi ssid and password");
            return ESP_OK;
        }
        ESP_LOGI(TAG, "Find provisioning devices");

        espnow_prov_responder_t responder_info = {
            .product_id = "debug_board"
        };

        espnow_prov_wifi_t wifi_config = {0x0};
        strcpy((char *)wifi_config.sta.ssid, prov_args.param->sval[0]);
        if (prov_args.param->sval[1]) {
            strcpy((char *)wifi_config.sta.password, prov_args.param->sval[1]);
        }

        /* For a lot of devices will connect to different aps with same ssid and password */
        wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

        s_device_num = 0;
        ret = espnow_prov_responder_start(&responder_info, pdMS_TO_TICKS(prov_args.initiator->ival[0]), &wifi_config, responder_recv_callback);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_prov_responder_start");

        ESP_LOGI(TAG, "Add device to the network: %s", prov_args.param->sval[0]);
    }

    return ESP_OK;
}

void register_provisioning()
{
    prov_args.erase = arg_lit0("e", "erase", "Reset WiFi provisioning information and restart");
    prov_args.responder = arg_lit0("r", "responder", "Responder devices start provisioning");
    prov_args.initiator = arg_int0("i", "initiator", "<beacon_time(s)>", "Set provisioning beacon time");
    prov_args.param = arg_strn(NULL, NULL, "<ap_ssid> <app_password>",
                               0, 2, "Configure network for devices");
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
            espnow_mac_str2hex(control_args.mac->sval[0], dest_addr);
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

static struct {
    struct arg_str *download;
    struct arg_int *find;
    struct arg_str *send;
    struct arg_end *end;
} ota_args;

static const esp_partition_t *g_ota_data_partition = NULL;
static size_t g_ota_size = 0;

static esp_err_t ota_initiator_data_cb(size_t src_offset, void* dst, size_t size)
{
    return esp_partition_read(g_ota_data_partition, src_offset, dst, size);
}

static esp_err_t firmware_download(const char *url)
{
#define OTA_DATA_PAYLOAD_LEN 1460

    esp_err_t ret       = ESP_OK;
    uint8_t *data       = ESP_MALLOC(OTA_DATA_PAYLOAD_LEN);
    char name[32]       = {0x0};
    size_t total_size   = 0;
    int start_time      = 0;
    esp_ota_handle_t ota_handle = 0;

    /**
     * @note If you need to upgrade all devices, pass MWIFI_ADDR_ANY;
     *       If you upgrade the incoming address list to the specified device
     */

    esp_http_client_config_t config = {
        .url            = url,
        .transport_type = HTTP_TRANSPORT_UNKNOWN,
        .timeout_ms     = 10 * 1000,
    };

    /**
     * @brief 1. Connect to the server
     */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    ESP_ERROR_GOTO(!client, EXIT, "Initialise HTTP connection");

    start_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Open HTTP connection: %s", url);

    /**
     * @brief First, the firmware is obtained from the http server and stored on the root node.
     */
    do {
        ret = esp_http_client_open(client, 0);

        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "<%s> Connection service failed", esp_err_to_name(ret));
        }
    } while (ret != ESP_OK);

    total_size = esp_http_client_fetch_headers(client);
    sscanf(url, "%*[^/]//%*[^/]/%[^.bin]", name);

    if (total_size <= 0) {
        ESP_LOGW(TAG, "Please check the address of the server");
        ret = esp_http_client_read(client, (char *)data, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(ret < 0, EXIT, "<%s> Read data from http stream", esp_err_to_name(ret));

        ESP_LOGW(TAG, "Recv data: %.*s", ret, data);
        goto EXIT;
    }

    /**< Get partition info of currently running app
    Return the next OTA app partition which should be written with a new firmware.*/
    const esp_partition_t *running = esp_ota_get_running_partition();
    g_ota_data_partition  = esp_ota_get_next_update_partition(NULL);

    ESP_ERROR_RETURN(!running || !g_ota_data_partition, ESP_ERR_ESPNOW_OTA_FIRMWARE_PARTITION,
                     "No partition is found or flash read operation failed");

    ESP_LOGI(TAG, "Running partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             running->label, running->type, running->subtype, running->address);
    ESP_LOGI(TAG, "Update partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             g_ota_data_partition->label, g_ota_data_partition->type, g_ota_data_partition->subtype, g_ota_data_partition->address);

    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(g_ota_data_partition, total_size, &ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_begin failed, total_size", esp_err_to_name(ret));

    /**
     * @brief 3. Read firmware from the server and write it to the flash of the root node
     */
    for (ssize_t size = 0, recv_size = 0, i = 0; recv_size < total_size; recv_size += size, ++i) {
        size = esp_http_client_read(client, (char *)data, OTA_DATA_PAYLOAD_LEN);
        ESP_ERROR_GOTO(size < 0, EXIT, "<%s> Read data from http stream", esp_err_to_name(ret));

        if (size > 0) {
            /**< Write OTA update data to partition */
            ret = esp_ota_write(ota_handle, data, size);
            ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> Write firmware to flash, size: %d, data: %.*s",
                esp_err_to_name(ret), size, size, data);

        } else {
            ESP_LOGW(TAG, "<%s> esp_http_client_read", esp_err_to_name(ret));
            goto EXIT;
        }

        if (i % 100 == 0 || recv_size == total_size) {
            ESP_LOGI(TAG, "Firmware download size: %d, progress rate: %d%%",
                     recv_size, recv_size * 100 / total_size);
        }
    }

    ESP_LOGI(TAG, "The service download firmware is complete, total_size: %d Spend time: %ds",
             total_size, (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000);
    
    g_ota_size = total_size;
    espnow_storage_set("binary_len", &total_size, sizeof(size_t));

    ret = esp_ota_end(ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_end", esp_err_to_name(ret));

EXIT:
    ESP_FREE(data);
    return ESP_OK;
}

static void ota_send_task(void *arg)
{
    size_t addrs_num = 0;
    espnow_ota_result_t ota_result = {0};
    uint32_t start_time = xTaskGetTickCount();
    uint8_t (* addrs_list)[ESPNOW_ADDR_LEN] = ESP_MALLOC(ESPNOW_ADDR_LEN);

    for (const char *tmp = (char *)arg;; tmp++) {
        if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
            espnow_mac_str2hex(tmp - 17, addrs_list[addrs_num]);
            addrs_num++;

            if (*tmp == '\0' || *(tmp + 1) == '\0') {
                break;
            }

            addrs_list = ESP_REALLOC(addrs_list, ESPNOW_ADDR_LEN * (addrs_num + 1));
        }
    }

    uint8_t sha_256[32] = {0};
    esp_partition_get_sha256(g_ota_data_partition, sha_256);
    ESP_LOG_BUFFER_HEXDUMP(TAG, sha_256, 32, ESP_LOG_DEBUG);
    espnow_ota_initiator_send(addrs_list, addrs_num, sha_256, g_ota_size,
                                    ota_initiator_data_cb, &ota_result);

    ESP_LOGI(TAG, "Firmware is sent to the device to complete, Spend time: %ds",
                (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000);
    ESP_LOGI(TAG, "Devices upgrade completed, successed_num: %d, unfinished_num: %d",
                ota_result.successed_num, ota_result.unfinished_num);

    ESP_FREE(addrs_list);
    espnow_ota_initiator_result_free(&ota_result);
    ESP_FREE(arg);

    vTaskDelete(NULL);
}

/**
 * @brief  A function which implements `ota` ota.
 */
static int ota_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &ota_args) != ESP_OK) {
        arg_print_errors(stderr, ota_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    if (ota_args.download->count) {
        ESP_LOGI(TAG, "Firmware Download, url: %s", ota_args.download->sval[0]);
        ret = firmware_download(ota_args.download->sval[0]);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> firmware_download", esp_err_to_name(ret));
    }

    if (ota_args.find->count) {
        ESP_LOGI(TAG, "Find upgradeable devices");

        size_t num = 0;
        espnow_ota_responder_t *info_list = NULL;
        ret = espnow_ota_initiator_scan(&info_list, &num, pdMS_TO_TICKS(ota_args.find->ival[0]));
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_ota_initiator_scan", esp_err_to_name(ret));

        if (num > 0) {
            char (*addrs_list)[18] = ESP_MALLOC(num * 18 + 1);

            for (int i = 0; i < num; ++i) {
                sprintf(addrs_list[i], MACSTR "|", MAC2STR(info_list[i].mac));
            }

            ESP_LOGI(TAG, "info, num: %d, list: %s", num, (char *)addrs_list);
            ESP_FREE(addrs_list);

            ESP_LOGI(TAG, "|         mac       | Channel | Rssi | Project name | ESP-IDF version | App version | Secure version | Compile time |");

            for (int i = 0; i < num; ++i) {
                ESP_LOGI(TAG, "| "MACSTR" |   %d   |  %d  | %12s | %15s | %11s | %d | %6s %6s |",
                         MAC2STR(info_list[i].mac), info_list[i].channel, info_list[i].rssi,
                         info_list[i].app_desc.project_name, info_list[i].app_desc.idf_ver, info_list[i].app_desc.version,
                         info_list[i].app_desc.secure_version, info_list[i].app_desc.date, info_list[i].app_desc.time);
            }
        }

        espnow_ota_initiator_scan_result_free();
    }

    if (ota_args.send->count) {
        ESP_LOGI(TAG, "Send firmware to selected device: %s", ota_args.send->sval[0]);

        if(!g_ota_data_partition && espnow_storage_get("binary_len", &g_ota_size, sizeof(size_t)) != ESP_OK){
            ESP_LOGE(TAG, "Firmware not downloaded");
            return ESP_FAIL;
        }

        if(!g_ota_data_partition) {
            g_ota_data_partition  = esp_ota_get_next_update_partition(NULL);
        };

        char *addrs_list = ESP_CALLOC(1, strlen(ota_args.send->sval[0]) + 1);
        strcpy(addrs_list, ota_args.send->sval[0]);

        xTaskCreate(ota_send_task, "ota_send", 8 * 1024, addrs_list, tskIDLE_PRIORITY + 1, NULL);
    }

    return ESP_OK;
}

void register_ota()
{
    ota_args.download = arg_str0("d", "download", "<url>", "Firmware Download");
    ota_args.find     = arg_int0("f", "find", "<wait_tick>", "Find upgradeable devices");
    ota_args.send     = arg_str0("s", "send", "<xx:xx:xx:xx:xx:xx>,<xx:xx:xx:xx:xx:xx>", "Send firmware to selected device");
    ota_args.end      = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "ota",
        .help = "Firmware update",
        .hint = NULL,
        .func = &ota_func,
        .argtable = &ota_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}


/**
 * @brief  A function which implements beacon command.
 */
static int beacon_func(int argc, char **argv)
{
    esp_err_t ret = ESP_OK;
    char *beacon_data = NULL;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    const esp_app_desc_t *app_desc = esp_app_get_description();
#else
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
#endif
    size_t beacon_data_len = 0;
    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame_head.security = CONFIG_ESPNOW_DEBUG_SECURITY;

    espnow_add_peer(g_src_addr, NULL);

    asprintf(&beacon_data, LOG_COLOR_I "I (%u) %s: project_name: %s, app_version: %s, esp-idf_version: %s, "
             "free_heap: %d, total_heap: %d, rx_rssi: %d, compile_time: %s %s\n",
             esp_log_timestamp(), __func__,
             app_desc->project_name, app_desc->version, app_desc->idf_ver,
             esp_get_free_heap_size(), heap_caps_get_total_size(MALLOC_CAP_DEFAULT), g_rx_ctrl.rssi,
             app_desc->date, app_desc->time);

    beacon_data_len = strlen(beacon_data) + 1;
    char *data = beacon_data;
    for (size_t size = MIN(beacon_data_len, ESPNOW_DATA_LEN);
            size > 0; data += size, beacon_data_len -= size, size = MIN(beacon_data_len, ESPNOW_DATA_LEN)) {
        ret = espnow_send(ESPNOW_DATA_TYPE_DEBUG_LOG, g_src_addr,
                        data, size, &frame_head, portMAX_DELAY);
    }

    espnow_del_peer(g_src_addr);
    free(beacon_data);
    return ret;
}

/**
 * @brief  Register beacon command.
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

static struct {
    struct arg_str *tag;
    struct arg_str *level;
    struct arg_str *mode;
    struct arg_str *flash;
    struct arg_lit *info;
    struct arg_end *end;
} log_args;

/**
 * @brief  A function which implements log command.
 */
static int log_func(int argc, char **argv)
{
    const char *level_str[6] = {"NONE", "ERR", "WARN", "INFO", "DEBUG", "VER"};
    espnow_log_config_t log_config = {0};
    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame_head.security = CONFIG_ESPNOW_DEBUG_SECURITY;

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

                espnow_send(ESPNOW_DATA_TYPE_DEBUG_LOG, ESPNOW_ADDR_BROADCAST,
                            log_data, size, &frame_head, portMAX_DELAY);
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
    struct arg_int *count;
    struct arg_int *len;
    struct arg_end *end;
} sec_test_args;

static int sec_test_func(int argc, char **argv)
{
    uint32_t data_len = 0;
    uint32_t count = 0;

    if (arg_parse(argc, argv, (void **) &sec_test_args) != ESP_OK) {
        arg_print_errors(stderr, sec_test_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (sec_test_args.len->count) {
        data_len = sec_test_args.len->ival[0];
    } else {
        data_len = ESPNOW_DATA_LEN;
    }

    if (sec_test_args.count->count) {
        count = sec_test_args.count->ival[0];
    } else {
        count = 100;
    }

    ESP_ERROR_RETURN(!data_len || !count, ESP_FAIL, "DATA length or Count is not valid");

    espnow_sec_t *espnow_sec = ESP_MALLOC(sizeof(espnow_sec_t));
    uint8_t key_info[APP_KEY_LEN];
    uint8_t *plain_txt = ESP_MALLOC(data_len);
    uint8_t *dec_txt = ESP_MALLOC(data_len);
    uint8_t *enc_txt = NULL;
    size_t length = 0;
    int64_t start_time = 0;
    int64_t end_time = 0;
    int64_t enc_time = 0;
    int64_t dec_time = 0;

    espnow_sec_init(espnow_sec);
    enc_txt = ESP_MALLOC(data_len + espnow_sec->tag_len);
    esp_fill_random(key_info, APP_KEY_LEN);
    int ret = espnow_sec_setkey(espnow_sec, key_info);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "espnow_sec_setkey %x", ret);

    for (int i = 0; i < count; i++) {
        esp_fill_random(plain_txt, data_len);
        start_time = esp_timer_get_time();
        espnow_sec_auth_encrypt(espnow_sec, plain_txt, data_len, enc_txt, data_len + espnow_sec->tag_len,
                                &length, espnow_sec->tag_len);
        end_time = esp_timer_get_time();
        espnow_sec_auth_decrypt(espnow_sec, enc_txt, data_len + espnow_sec->tag_len, dec_txt, data_len,
                                &length, espnow_sec->tag_len);

        dec_time += esp_timer_get_time() - end_time;
        enc_time += end_time - start_time;

        if (memcmp(plain_txt, dec_txt, data_len)) {
            ESP_LOGE(TAG, "Decrypt error");
        }
    }

    ESP_LOGI(TAG, "Encrypting data of %d bytes takes an average of %lld us", data_len, enc_time/count);
    ESP_LOGI(TAG, "Decrypting data of %d bytes takes an average of %lld us", data_len, dec_time/count);

EXIT:
    espnow_sec_deinit(espnow_sec);
    ESP_FREE(plain_txt);
    ESP_FREE(enc_txt);
    ESP_FREE(dec_txt);
    ESP_FREE(espnow_sec);

    return ESP_OK;
}

/**
 * @brief  Register security test command.
 */
static void register_security_test()
{
    sec_test_args.count = arg_int0("c", "count", "<count>", "The number of times to encrypt and decrypt (Defaults: 100)");
    sec_test_args.len   = arg_int0("l", "len", "<len (Bytes)>", "The length of the data to be encrypted (Defaults: 230 Bytes)");
    sec_test_args.end   = arg_end(8);

    const esp_console_cmd_t cmd = {
        .command = "sec_test",
        .help = "Test encryption and decryption time",
        .hint = NULL,
        .func = &sec_test_func,
        .argtable = &sec_test_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_lit *erase;
    struct arg_int *find;
    struct arg_str *send;
    struct arg_end *end;
} sec_args;

static void sec_send_task(void *arg)
{
    size_t addrs_num = 0;
    espnow_sec_result_t sec_result = {0};
    uint32_t start_time = xTaskGetTickCount();
    uint8_t (* addrs_list)[ESPNOW_ADDR_LEN] = ESP_MALLOC(ESPNOW_ADDR_LEN);

    for (const char *tmp = (char *)arg;; tmp++) {
        if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
            espnow_mac_str2hex(tmp - 17, addrs_list[addrs_num]);
            addrs_num++;

            if (*tmp == '\0' || *(tmp + 1) == '\0') {
                break;
            }

            addrs_list = ESP_REALLOC(addrs_list, ESPNOW_ADDR_LEN * (addrs_num + 1));
        }
    }

    uint8_t key_info[APP_KEY_LEN];
    espnow_get_key(key_info);
    espnow_sec_initiator_start(key_info, "espnow_pop", addrs_list, addrs_num, &sec_result);

    ESP_LOGI(TAG, "App key is sent to the device to complete, Spend time: %dms",
             (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Devices security completed, successed_num: %d, unfinished_num: %d", 
             sec_result.successed_num, sec_result.unfinished_num);

    ESP_FREE(addrs_list);
    espnow_sec_initiator_result_free(&sec_result);
    ESP_FREE(arg);

    vTaskDelete(NULL);
}

static int sec_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &sec_args) != ESP_OK) {
        arg_print_errors(stderr, sec_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    if (sec_args.erase->count) {
        ESP_LOGI(TAG, "Erase key info and restart");
        ret = espnow_erase_key();
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> erase_key", esp_err_to_name(ret));
        esp_restart();
    }

    if (sec_args.find->count) {
        ESP_LOGI(TAG, "Find devices waiting to get key");

        size_t num = 0;
        espnow_sec_responder_t *info_list = NULL;
        ret = espnow_sec_initiator_scan(&info_list, &num, pdMS_TO_TICKS(sec_args.find->ival[0]));
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_sec_initiator_scan", esp_err_to_name(ret));

        if (num > 0) {
            char (*addrs_list)[18] = ESP_MALLOC(num * 18 + 1);

            for (int i = 0; i < num; ++i) {
                sprintf(addrs_list[i], MACSTR "|", MAC2STR(info_list[i].mac));
            }

            ESP_LOGI(TAG, "info, num: %d, list: %s", num, (char *)addrs_list);
            ESP_FREE(addrs_list);

            ESP_LOGI(TAG, "|         mac       | Channel | Rssi | Security version |");

            for (int i = 0; i < num; ++i) {
                ESP_LOGI(TAG, "| "MACSTR" |   %d   |  %d  | %d |",
                         MAC2STR(info_list[i].mac), info_list[i].channel, info_list[i].rssi,
                         info_list[i].sec_ver);
            }
        }

        espnow_sec_initiator_scan_result_free();
    }

    if (sec_args.send->count) {
        ESP_LOGI(TAG, "Security initiator start: %s", sec_args.send->sval[0]);

        uint8_t key_info[APP_KEY_LEN];
        if(espnow_get_key(key_info) != ESP_OK) {
            ESP_LOGE(TAG, "Secure key is not set");
            return ESP_FAIL;
        };

        char *addrs_list = ESP_CALLOC(1, strlen(sec_args.send->sval[0]) + 1);
        strcpy(addrs_list, sec_args.send->sval[0]);

        xTaskCreate(sec_send_task, "sec_send", 8 * 1024, addrs_list, tskIDLE_PRIORITY + 1, NULL);
    }

    return ESP_OK;
}

/**
 * @brief  Register security command.
 */
static void register_security()
{
    sec_args.erase   = arg_lit0("e", "erase", "Erase the key in flash and restart");
    sec_args.find = arg_int0("f", "find", "<wait_s>", "Find devices waiting to get key");
    sec_args.send   = arg_str0("s", "send", "<<xx:xx:xx:xx:xx:xx>,<xx:xx:xx:xx:xx:xx>>", "Security handshake with responder devices and send security key");
    sec_args.end   = arg_end(8);

    const esp_console_cmd_t cmd = {
        .command = "security",
        .help = "Security",
        .hint = NULL,
        .func = &sec_func,
        .argtable = &sec_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void register_espnow()
{
    register_command();
    register_scan();
    register_provisioning();
    register_control();
    register_ota();
    register_espnow_beacon();
    register_log();
    register_security_test();
    register_security();
}
