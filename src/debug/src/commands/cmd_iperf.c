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

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "esp_timer.h"

#include "espnow.h"
#include "espnow_console.h"
#include "espnow_ctrl.h"
#include "espnow_log.h"
#include "espnow_ota.h"
#include "espnow_prov.h"
#include "espnow_utils.h"

#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <errno.h>
#include <sys/param.h>
#include "driver/gpio.h"

static const char *TAG = "iperf_cmd";

static struct {
    struct arg_int *channel;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 3, 0)
    struct arg_int *rate;
#endif
    struct arg_int *tx_power;
    struct arg_int *protocol;
    struct arg_str *country_code;
    struct arg_lit *info;
    struct arg_end *end;
} espnow_config_args;

static int espnow_config_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &espnow_config_args) != ESP_OK) {
        arg_print_errors(stderr, espnow_config_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (espnow_config_args.info->count) {
        int8_t power = 0;
        uint8_t protocol_bitmap = 0;
        uint8_t primary = 0;
        wifi_country_t country    = {0};
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;

        esp_wifi_get_country(&country);
        esp_wifi_get_channel(&primary, &second);
        esp_wifi_get_max_tx_power(&power);
        esp_wifi_get_protocol(ESP_IF_WIFI_STA, &protocol_bitmap);

        ESP_LOGI(TAG, "Channel, primary: %d, second: %d", primary, second);
        ESP_LOGI(TAG, "Maximum transmiting power: %d", power);
        ESP_LOGI(TAG, "Wi-Fi protocol bitmap: 0x%02x", protocol_bitmap);
        country.cc[2] = '\0';
        ESP_LOGI(TAG, "Wi-Fi country code: %s", country.cc);
        return ESP_OK;
    }

    if (espnow_config_args.country_code->count) {
        wifi_country_t country = {0};
        const char *country_code = espnow_config_args.country_code->sval[0];

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

        esp_wifi_set_country(&country);
    }

    if (espnow_config_args.channel->count) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(espnow_config_args.channel->ival[0], WIFI_SECOND_CHAN_NONE));
    }

    if (espnow_config_args.rate->count) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
        if (espnow_config_args.rate->count == 4) {
            uint8_t peer[ESPNOW_ADDR_LEN] = {0};
            esp_now_rate_config_t rate_config = {
                .phymode = espnow_config_args.rate->ival[0],
                .rate = espnow_config_args.rate->ival[1],
                .ersu = espnow_config_args.rate->ival[2],
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 3)
                .dcm = espnow_config_args.rate->ival[3],
#endif
            };
            ESP_LOGI(TAG, "Set rate config: phymode %d, rate %d, ersu %d, dcm %d", rate_config.phymode, rate_config.rate, rate_config.ersu, espnow_config_args.rate->ival[3]);
            ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, peer));
            ESP_ERROR_CHECK(esp_now_set_peer_rate_config(peer, &rate_config));
        } else {
            ESP_LOGE(TAG, "Loss rate config value");
            return ESP_FAIL;
        }
#else
        ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, espnow_config_args.rate->ival[0]));
#endif
    }

    if (espnow_config_args.tx_power->count) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(espnow_config_args.tx_power->ival[0]));
    }

    if (espnow_config_args.protocol->count) {
        ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, espnow_config_args.protocol->ival[0]));
    }

    return ESP_OK;
}

void register_espnow_config()
{
    espnow_config_args.channel     = arg_int0("c", "channel", "<channel (1 ~ 13)>", "Channel of ESP-NOW");
    espnow_config_args.country_code  = arg_str0("C", "country_code", "<country_code ('CN', 'JP, 'US')>", "Set the current country code");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    espnow_config_args.rate     = arg_intn("r", "rate", "<phymode (wifi_phy_mode_t) rate (wifi_phy_rate_t) ersu (0|1) dcm (0|1)>", 0, 4, "Wi-Fi PHY rate encodings");
#else
    espnow_config_args.rate     = arg_int0("r", "rate", "<rate (wifi_phy_rate_t)>", "Wi-Fi PHY rate encodings");
#endif
    espnow_config_args.protocol = arg_int0("p", "protocol", "<protocol_bitmap[1, 2, 4, 8]>", "Set protocol type of specified interface");
    espnow_config_args.tx_power = arg_int0("t", "tx_power", "<tx_power ([8, 84])>", "Set maximum transmitting power after WiFi start");
    espnow_config_args.info     = arg_lit0("i", "info", "Print all configuration information");
    espnow_config_args.end      = arg_end(9);

    const esp_console_cmd_t cmd = {
        .command = "espnow_config",
        .help = "ESP-NOW configuration",
        .hint = NULL,
        .func = &espnow_config_func,
        .argtable = &espnow_config_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

struct espnow_iperf_cfg {
    bool finish;
    uint16_t packet_len;
    uint16_t transmit_time;
    uint32_t ping_count;
    uint16_t report_interval;
    espnow_data_type_t type;
    espnow_frame_head_t frame_head;
    uint8_t addr[6];
    int gpio_num;
} g_iperf_cfg = {
    .finish = true,
    .packet_len      = ESPNOW_DATA_LEN,
    .transmit_time   = 60,
    .report_interval = 3,
    .ping_count      = 64,
    .frame_head.group     = false,
    .type            = ESPNOW_DATA_TYPE_RESERVED,
};

typedef enum {
    IPERF_BANDWIDTH,
    IPERF_BANDWIDTH_STOP,
    IPERF_BANDWIDTH_STOP_ACK,
    IPERF_PING,
    IPERF_PING_ACK,
} iperf_type_t;

typedef struct {
    iperf_type_t type;
    uint32_t seq;
    uint8_t data[0];
} espnow_iperf_data_t;

#define IPERF_QUEUE_SIZE 10
static QueueHandle_t g_iperf_queue = NULL;
typedef struct {
    uint8_t src_addr[6];
    void *data;
    size_t size;
    wifi_pkt_rx_ctrl_t rx_ctrl;
} iperf_recv_data_t;

static esp_err_t espnow_iperf_initiator_recv(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    if (g_iperf_queue) {
        iperf_recv_data_t iperf_data = { 0 };
        iperf_data.data = ESP_MALLOC(size);
        if (!iperf_data.data) {
            return ESP_FAIL;
        }
        memcpy(iperf_data.data, data, size);
        iperf_data.size = size;
        memcpy(iperf_data.src_addr, src_addr, 6);
        memcpy(&iperf_data.rx_ctrl, rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
        if (xQueueSend(g_iperf_queue, &iperf_data, 0) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send iperf recv queue failed", __func__, __LINE__);
            ESP_FREE(iperf_data.data);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

static void espnow_iperf_initiator_task(void *arg)
{
    esp_err_t ret   = ESP_OK;
    espnow_iperf_data_t *iperf_data = ESP_CALLOC(1, g_iperf_cfg.packet_len);
    iperf_data->type = IPERF_BANDWIDTH;
    iperf_data->seq   = 0;


    int64_t start_time = esp_timer_get_time();
    int64_t end_time = start_time + g_iperf_cfg.transmit_time * 1000 * 1000;
    uint32_t total_count   = 0;

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_add_peer(g_iperf_cfg.addr, NULL);
    }

    ESP_LOGI(TAG, "[  Responder MAC  ]   Interval     Transfer     Frame_rate     Bandwidth");

    for (int64_t report_time = start_time + g_iperf_cfg.report_interval * 1000 * 1000, report_count = 0;
            esp_timer_get_time() < end_time && !g_iperf_cfg.finish;) {
        ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          g_iperf_cfg.packet_len, &g_iperf_cfg.frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK && ret != ESP_ERR_WIFI_TIMEOUT, "<%s> espnow_send", esp_err_to_name(ret));
        iperf_data->seq++;
        ++total_count;

        if (esp_timer_get_time() >= report_time) {
            uint32_t report_time_s = (report_time - start_time) / (1000 * 1000);
            double report_size    = (iperf_data->seq - report_count) * g_iperf_cfg.packet_len / 1e6;

            ESP_LOGI(TAG, "["MACSTR"]  %2d-%2d sec  %2.2f MBytes   %0.2f Hz  %0.2f Mbps",
                     MAC2STR(g_iperf_cfg.addr), report_time_s - g_iperf_cfg.report_interval, report_time_s,
                     report_size, (iperf_data->seq - report_count) * 1.0 / g_iperf_cfg.report_interval, report_size * 8 / g_iperf_cfg.report_interval);

            report_time = esp_timer_get_time() + g_iperf_cfg.report_interval * 1000 * 1000;
            report_count = iperf_data->seq;
        }
    }

    iperf_data->type  = IPERF_BANDWIDTH_STOP;
    int retry_count     = 5;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    uint32_t spend_time_ms = (esp_timer_get_time() - start_time) / 1000;

    do {
        ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          g_iperf_cfg.packet_len, &g_iperf_cfg.frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        memset(iperf_data, 0, g_iperf_cfg.packet_len);
        iperf_recv_data_t recv_data = { 0 };
        if (g_iperf_queue && xQueueReceive(g_iperf_queue, &recv_data, pdMS_TO_TICKS(1000)) == pdPASS) {
            ret = ESP_OK;
            memcpy(iperf_data, recv_data.data, recv_data.size);
            memcpy(g_iperf_cfg.addr, recv_data.src_addr, 6);
            memcpy(&rx_ctrl, &recv_data.rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
            ESP_FREE(recv_data.data);
        } else {
            ret = ESP_FAIL;
        }
    } while (ret != ESP_OK && retry_count-- > 0 && iperf_data->type != IPERF_BANDWIDTH_STOP_ACK);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "<%s> Receive responder response failed", esp_err_to_name(ret));
    } else {
        uint32_t write_count = iperf_data->seq > 0 ? iperf_data->seq - 1 : 0;
        uint32_t lost_count  = total_count - write_count;
        double total_len     = (total_count * g_iperf_cfg.packet_len) / 1e6;

        if (total_count && write_count && spend_time_ms) {
            ESP_LOGI(TAG, "initiator Report:");
            ESP_LOGI(TAG, "[ ID] Interval      Transfer       Bandwidth      Jitter   Lost/Total Datagrams  RSSI  Channel");
            ESP_LOGI(TAG, "[000] %2d-%2d sec    %2.2f MBytes    %0.2f Mbps    %0.2f ms    %d/%d (%0.2f%%)    %d    %d",
                    0, spend_time_ms / 1000, total_len, total_len * 8 * 1000 / spend_time_ms, spend_time_ms * 1.0 / write_count,
                    lost_count, total_count, lost_count * 100.0 / total_count, rx_ctrl.rssi, rx_ctrl.channel);
        }
    }

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_del_peer(g_iperf_cfg.addr);
    }

    ESP_FREE(iperf_data);
    g_iperf_cfg.finish = true;

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_RESERVED, 0, NULL);
    if (g_iperf_queue) {
        iperf_recv_data_t tmp_data =  { 0 };

        while (xQueueReceive(g_iperf_queue, &tmp_data, 0)) {
            ESP_FREE(tmp_data.data);
        }

        vQueueDelete(g_iperf_queue);
        g_iperf_queue = NULL;
    }

    vTaskDelete(NULL);
}

static esp_err_t espnow_iperf_responder(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret         = ESP_OK;
    espnow_iperf_data_t *iperf_data   = (espnow_iperf_data_t *)data;
    static int64_t start_time;
    static uint32_t recv_count;
    static int64_t report_time;
    static uint32_t report_count;

    memcpy(g_iperf_cfg.addr, src_addr, 6);

    if (!g_iperf_cfg.finish) {
        recv_count++;

        if (iperf_data->seq == 0) {
            recv_count   = 0;
            start_time  = esp_timer_get_time();
            report_time = start_time + g_iperf_cfg.report_interval * 1000 * 1000;
            report_count = 0;
        } 

        if (iperf_data->type == IPERF_BANDWIDTH && esp_timer_get_time() >= report_time) {
            uint32_t report_time_s = (report_time - start_time) / (1000 * 1000);
            double report_size    = (recv_count - report_count) * size / 1e6;
            ESP_LOGI(TAG, "["MACSTR"]  %2d-%2d sec  %2.2f MBytes  %0.2f Mbps  %d dbm",
                     MAC2STR(g_iperf_cfg.addr), report_time_s - g_iperf_cfg.report_interval, report_time_s,
                     report_size, report_size * 8 / g_iperf_cfg.report_interval, rx_ctrl->rssi);

            report_time = esp_timer_get_time() + g_iperf_cfg.report_interval * 1000 * 1000;
            report_count = recv_count;
        } else if (iperf_data->type == IPERF_PING) {
            ESP_LOGV(TAG, "Recv IPERF_PING, seq: %d, recv_count: %d", iperf_data->seq, recv_count);
            iperf_data->type = IPERF_PING_ACK;

            if (g_iperf_cfg.gpio_num >= 0) {
                gpio_set_level(g_iperf_cfg.gpio_num, 0);
            }

            if (!g_iperf_cfg.frame_head.broadcast) {
                espnow_add_peer(g_iperf_cfg.addr, NULL);
            }

            ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                              size, &g_iperf_cfg.frame_head, portMAX_DELAY);

            if (!g_iperf_cfg.frame_head.broadcast) {
                espnow_del_peer(g_iperf_cfg.addr);
            }

            if (g_iperf_cfg.gpio_num >= 0) {
                gpio_set_level(g_iperf_cfg.gpio_num, 1);
            }

            ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_send", esp_err_to_name(ret));
        } else if (iperf_data->type == IPERF_BANDWIDTH_STOP) {
            uint32_t total_count = iperf_data->seq + 1;
            uint32_t lost_count  = total_count - recv_count;
            double total_len     = (total_count * size) / 1e6;
            uint32_t spend_time_ms  = (esp_timer_get_time() - start_time) / 1000;

            ESP_LOGI(TAG, "[ ID] Interval      Transfer       Bandwidth      Jitter   Lost/Total Datagrams");
            ESP_LOGI(TAG, "[000] %2d-%2d sec    %2.2f MBytes    %0.2f Mbps    %0.2f ms    %d/%d (%0.2f%%)",
                     0, spend_time_ms / 1000, total_len, total_len * 8 * 1000 / spend_time_ms, spend_time_ms * 1.0 / recv_count,
                     lost_count, total_count, lost_count * 100.0 / total_count);

            iperf_data->seq = recv_count;
            iperf_data->type = IPERF_BANDWIDTH_STOP_ACK;
            ESP_LOGD(TAG, "iperf_data->seq: %d",  iperf_data->seq);

            espnow_frame_head_t frame_head = {
                .filter_adjacent_channel = true,
            };

            espnow_add_peer(g_iperf_cfg.addr, NULL);
            ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                              sizeof(espnow_iperf_data_t), &frame_head, portMAX_DELAY);
            ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_send", esp_err_to_name(ret));

            espnow_del_peer(g_iperf_cfg.addr);
        }
    }

    return ESP_OK;
}

static void espnow_iperf_responder_start(void)
{
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_RESERVED, 1, espnow_iperf_responder);
    ESP_LOGI(TAG, "[  Initiator MAC  ] Interval       Transfer     Bandwidth   RSSI");
}

static void espnow_iperf_ping_task(void *arg)
{
    esp_err_t ret     = ESP_OK;
    espnow_iperf_data_t *iperf_data = ESP_CALLOC(1, g_iperf_cfg.packet_len);
    size_t send_count = 0;
    size_t recv_count = 0;
    uint32_t spend_time_ms = 0;
    int64_t s_time = esp_timer_get_time();

    size_t recv_size      = 0;
    uint32_t max_time_ms  = 0;
    uint32_t min_time_ms  = UINT32_MAX;
    uint32_t res_time_ms  = 0;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_add_peer(g_iperf_cfg.addr, NULL);
    }

    for (; send_count < g_iperf_cfg.ping_count && !g_iperf_cfg.finish; send_count ++) {
        iperf_data->seq = send_count;
        iperf_data->type = IPERF_PING;

        if (g_iperf_cfg.gpio_num >= 0) {
            gpio_set_level(g_iperf_cfg.gpio_num, 0);
        }

        int64_t start_time = esp_timer_get_time();
        ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          g_iperf_cfg.packet_len, &g_iperf_cfg.frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        do {
            iperf_recv_data_t recv_data = { 0 };
            if (g_iperf_queue && xQueueReceive(g_iperf_queue, &recv_data, pdMS_TO_TICKS(3000)) == pdPASS) {
                ret = ESP_OK;
                memcpy(iperf_data, recv_data.data, recv_data.size);
                memcpy(g_iperf_cfg.addr, recv_data.src_addr, 6);
                memcpy(&rx_ctrl, &recv_data.rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
                recv_size = recv_data.size;
                ESP_FREE(recv_data.data);
            } else {
                ret = ESP_FAIL;
            }

            if (ret == ESP_OK && (iperf_data->type != IPERF_PING_ACK || iperf_data->seq != send_count)) {
                ESP_LOGW(TAG, "data_size: %d, send_seq: %d, recv_seq: %d", recv_size, send_count, iperf_data->seq);
            }
        } while (ret == ESP_OK && (iperf_data->type != IPERF_PING_ACK || iperf_data->seq != send_count));

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "seq=%d Destination Host Unreachable", iperf_data->seq);
            continue;
        }

        if (g_iperf_cfg.gpio_num >= 0) {
            gpio_set_level(g_iperf_cfg.gpio_num, 1);
        }

        res_time_ms = (uint32_t)((esp_timer_get_time() - start_time) / 1000);
        spend_time_ms += res_time_ms;

        max_time_ms = MAX(max_time_ms, res_time_ms);
        min_time_ms = MIN(min_time_ms, res_time_ms);

        recv_count++;
        ESP_LOGI(TAG, "%d bytes from " MACSTR ": seq=%d rssi=%d time=%d ms",
                 recv_size, MAC2STR(g_iperf_cfg.addr), iperf_data->seq, rx_ctrl.rssi, res_time_ms);

        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_recv", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "initiator ping report:");
    ESP_LOGI(TAG, "ping statistics for " MACSTR, MAC2STR(g_iperf_cfg.addr));
    ESP_LOGI(TAG, "%d packets transmitted, %d received, %0.2f%% packet loss, time: total %d ms, max: %d, min: %d, average %0.2f ms",
             send_count, recv_count, (send_count - recv_count) * 100.0 / send_count,
             (int)((esp_timer_get_time() - s_time) / 1000), max_time_ms, min_time_ms, spend_time_ms * 1.0 / recv_count);

    g_iperf_cfg.finish = true;

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_del_peer(g_iperf_cfg.addr);
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_RESERVED, 0, NULL);
    if (g_iperf_queue) {
        iperf_recv_data_t tmp_data =  { 0 };

        while (xQueueReceive(g_iperf_queue, &tmp_data, 0)) {
            ESP_FREE(tmp_data.data);
        }

        vQueueDelete(g_iperf_queue);
        g_iperf_queue = NULL;
    }

    ESP_FREE(iperf_data);
    vTaskDelete(NULL);
}

static struct {
    struct arg_str *initiator;
    struct arg_lit *responder;
    struct arg_lit *ping;
    struct arg_int *count;
    struct arg_int *interval;
    struct arg_int *len;
    struct arg_int *time;
    struct arg_int *broadcast;
    struct arg_lit *group;
    struct arg_lit *ack;
    struct arg_int *gpio;
    struct arg_lit *abort;
    struct arg_end *end;
} espnow_iperf_args;

static esp_err_t espnow_iperf_func(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **) &espnow_iperf_args) != ESP_OK) {
        arg_print_errors(stderr, espnow_iperf_args.end, argv[0]);
        return ESP_FAIL;
    }

    if (espnow_iperf_args.abort->count) {
        g_iperf_cfg.finish = true;
        return ESP_OK;
    }

    if (!g_iperf_cfg.finish) {
        ESP_LOGW(TAG, "ESPNOW iperf is running");
        return ESP_ERR_NOT_SUPPORTED;
    }

    if ((espnow_iperf_args.initiator->count && espnow_iperf_args.responder->count)
            || (!espnow_iperf_args.initiator->count && !espnow_iperf_args.responder->count)) {
        ESP_LOGW(TAG, "Should specific initiator/responder mode");
        return ESP_ERR_INVALID_ARG;
    }

    if (espnow_iperf_args.len->count) {
        g_iperf_cfg.packet_len = espnow_iperf_args.len->ival[0];
    } else {
        g_iperf_cfg.packet_len = ESPNOW_DATA_LEN;
    }

    if (espnow_iperf_args.interval->count) {
        g_iperf_cfg.report_interval = espnow_iperf_args.interval->ival[0];
    } else {
        g_iperf_cfg.report_interval = 3;
    }

    if (espnow_iperf_args.time->count) {
        g_iperf_cfg.transmit_time = espnow_iperf_args.time->ival[0];
    } else {
        g_iperf_cfg.transmit_time = 60;
    }

    if (espnow_iperf_args.group->count) {
        g_iperf_cfg.frame_head.group = true;
    } else {
        g_iperf_cfg.frame_head.group = false;
    }

    if (espnow_iperf_args.broadcast->count) {
        g_iperf_cfg.frame_head.broadcast        = true;
        g_iperf_cfg.frame_head.retransmit_count = espnow_iperf_args.broadcast->ival[0];
    } else {
        g_iperf_cfg.frame_head.broadcast        = false;
        g_iperf_cfg.frame_head.retransmit_count = 0;
    }

    if (espnow_iperf_args.count->count) {
        g_iperf_cfg.ping_count = espnow_iperf_args.count->ival[0];
    } else {
        g_iperf_cfg.ping_count = 64;
    }

    if (espnow_iperf_args.ack->count) {
        g_iperf_cfg.frame_head.ack = true;
    } else {
        g_iperf_cfg.frame_head.ack = 0;
    }

    if (espnow_iperf_args.gpio->count) {
        g_iperf_cfg.gpio_num = espnow_iperf_args.gpio->ival[0];
        gpio_reset_pin(g_iperf_cfg.gpio_num);
        gpio_set_direction(g_iperf_cfg.gpio_num, GPIO_MODE_OUTPUT);
        gpio_set_level(g_iperf_cfg.gpio_num, 1);
    } else {
        g_iperf_cfg.gpio_num = -1;
    }

    uint8_t sta_mac[ESPNOW_ADDR_LEN] = {0};
    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);

    uint8_t channel                = 1;
    wifi_second_chan_t second      = 0;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&channel, &second));

    g_iperf_cfg.finish = false;

    // esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_MCS7_SGI);

    if (espnow_iperf_args.initiator->count) {
        ESP_ERROR_RETURN(!espnow_mac_str2hex(espnow_iperf_args.initiator->sval[0], g_iperf_cfg.addr), ESP_ERR_INVALID_ARG,
                         "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");

        ESP_LOGI(TAG, "------------------------------------------------------------");
        ESP_LOGI(TAG, "initiator " MACSTR " send to " MACSTR, MAC2STR(sta_mac), MAC2STR(g_iperf_cfg.addr));
        ESP_LOGI(TAG, "espnow channel: %d", channel);
        ESP_LOGI(TAG, "------------------------------------------------------------");
        ESP_LOGI(TAG, "time: %d, interval: %d, len: %d", g_iperf_cfg.transmit_time, g_iperf_cfg.report_interval,
                 g_iperf_cfg.packet_len);

        g_iperf_queue = xQueueCreate(IPERF_QUEUE_SIZE, sizeof(iperf_recv_data_t));
        ESP_ERROR_RETURN(!g_iperf_queue, ESP_FAIL, "Create espnow recv queue fail");
        espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_RESERVED, 1, espnow_iperf_initiator_recv);

        if (espnow_iperf_args.ping->count) {
            xTaskCreate(espnow_iperf_ping_task, "espnow_iperf_ping", 4 * 1024,
                        NULL, tskIDLE_PRIORITY + 1, NULL);
        } else {
            xTaskCreate(espnow_iperf_initiator_task, "espnow_iperf_initiator", 4 * 1024,
                        NULL, tskIDLE_PRIORITY + 1, NULL);
        }
    }

    if (espnow_iperf_args.responder->count) {
        ESP_LOGI(TAG, "------------------------------------------------------------");
        ESP_LOGI(TAG, "responder " MACSTR " listening", MAC2STR(sta_mac));
        ESP_LOGI(TAG, "ESP-NOW window size: 230 bytes");;
        ESP_LOGI(TAG, "------------------------------------------------------------");
        espnow_iperf_responder_start();
    }

    return ESP_OK;
}

void register_espnow_iperf()
{
    espnow_iperf_args.initiator = arg_str0("c", "initiator", "<responder (xx:xx:xx:xx:xx:xx)>", "run in initiator mode, ping to <responder>");
    espnow_iperf_args.responder = arg_lit0("s", "responder", "run in responder mode, receive from throughput or ping");
    espnow_iperf_args.ping      = arg_lit0("p", "ping", "run in ping mode, send to <responder>");
    espnow_iperf_args.count     = arg_int0("C", "count", "<count>", "Stop ping after <count> replies (Defaults: 64)");
    espnow_iperf_args.interval  = arg_int0("i", "interval", "<interval (sec)>", "seconds between periodic bandwidth reports (default 3 secs)");
    espnow_iperf_args.time      = arg_int0("t", "time", "<time (sec)>", "time in seconds to transmit for (default 10 secs)");
    espnow_iperf_args.len       = arg_int0("l", "len", "<len (Bytes)>", "length of buffer in bytes to read or write (Defaults: 230 Bytes)");
    espnow_iperf_args.broadcast = arg_int0("b", "broadcast", "<count>", "Send package by broadcast");
    espnow_iperf_args.group     = arg_lit0("g", "group", "Send a package to a group");
    espnow_iperf_args.ack       = arg_lit0("A", "ack", "Wait for the receiving device to return ack to ensure transmission reliability");
    espnow_iperf_args.gpio      = arg_int0("G", "gpio", "<num>", "gpio number(default -1)");
    espnow_iperf_args.abort     = arg_lit0("a", "abort", "abort running espnow-iperf");
    espnow_iperf_args.end       = arg_end(6);

    const esp_console_cmd_t cmd = {
        .command  = "espnow_iperf",
        .help     = "ESP-NOW iperf",
        .hint     = NULL,
        .func     = &espnow_iperf_func,
        .argtable = &espnow_iperf_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void register_iperf(void)
{
    register_espnow_config();
    register_espnow_iperf();
}
