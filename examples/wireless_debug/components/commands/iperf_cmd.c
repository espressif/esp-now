/* Iperf commands

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

static const char *TAG = "iperf_cmd";

static struct {
    struct arg_int *channel;
    struct arg_int *rate;
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
        ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, espnow_config_args.rate->ival[0]));
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
    espnow_config_args.rate     = arg_int0("r", "rate", "<rate (wifi_phy_rate_t)>", "Wi-Fi PHY rate encodings");
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
    espnow_type_t type;
    espnow_frame_head_t frame_head;
    uint8_t addr[6];
} g_iperf_cfg = {
    .finish = true,
    .packet_len      = ESPNOW_DATA_LEN,
    .transmit_time   = 60,
    .report_interval = 3,
    .ping_count      = 64,
    .frame_head.group     = false,
    .type            = ESPNOW_TYPE_RESERVED,
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

static void espnow_iperf_initiator_task(void *arg)
{
    esp_err_t ret   = ESP_OK;
    espnow_iperf_data_t *iperf_data = ESP_CALLOC(1, g_iperf_cfg.packet_len);
    iperf_data->type = IPERF_BANDWIDTH;
    iperf_data->seq   = 0;


    uint32_t start_time = esp_timer_get_time();
    uint32_t end_time = start_time + g_iperf_cfg.transmit_time * 1000 * 1000;
    uint32_t total_count   = 0;

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_add_peer(g_iperf_cfg.addr, NULL);
    }

    ESP_LOGI(TAG, "[  Responder MAC  ]   Interval     Transfer     Frame_rate     Bandwidth");

    for (uint32_t report_time = start_time + g_iperf_cfg.report_interval * 1000 * 1000, report_count = 0;
            esp_timer_get_time() < end_time && !g_iperf_cfg.finish;) {
        ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          g_iperf_cfg.packet_len, &g_iperf_cfg.frame_head, portMAX_DELAY);
        ESP_ERROR_BREAK(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));
        iperf_data->seq++;
        ++total_count;

        if (esp_timer_get_time() >= report_time) {
            uint32_t report_time_s = (report_time - start_time) / (1000 * 1000);
            double report_size    = (iperf_data->seq - report_count) * g_iperf_cfg.packet_len / 1e6;

            ESP_LOGI(TAG, "["MACSTR"]  %2d-%2d sec  %2.2f MBytes   %0.2f Hz  %0.2f Mbits/sec",
                     MAC2STR(g_iperf_cfg.addr), report_time_s - g_iperf_cfg.report_interval, report_time_s,
                     (iperf_data->seq - report_count) * 1.0 / g_iperf_cfg.report_interval, report_size, report_size * 8 / g_iperf_cfg.report_interval);

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

        size_t recv_size = 0;
        memset(iperf_data, 0, g_iperf_cfg.packet_len);
        ret = espnow_recv(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          &recv_size, &rx_ctrl, pdMS_TO_TICKS(1000));
    } while (ret != ESP_OK && retry_count-- > 0 && iperf_data->type != IPERF_BANDWIDTH_STOP_ACK);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "<%s> Receive responder response failed", esp_err_to_name(ret));
    } else {
        uint32_t write_count = iperf_data->seq > 0 ? iperf_data->seq + 1 : 0;
        uint32_t lost_count  = total_count - write_count;
        double total_len     = (total_count * g_iperf_cfg.packet_len) / 1e6;

        if (total_count && write_count && spend_time_ms) {
            ESP_LOGI(TAG, "initiator Report:");
            ESP_LOGI(TAG, "[ ID] Interval      Transfer       Bandwidth      Jitter   Lost/Total Datagrams  RSSI  Channel");
            ESP_LOGI(TAG, "[000] %2d-%2d sec    %2.2f MBytes    %0.2f Mbits/sec    %0.2f ms    %d/%d (%0.2f%%)    %d    %d",
                    0, spend_time_ms / 1000, total_len, total_len * 8 * 1000 / spend_time_ms, spend_time_ms * 1.0 / write_count,
                    lost_count, total_count, lost_count * 100.0 / total_count, rx_ctrl.rssi, rx_ctrl.channel);
        }
    }

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_del_peer(g_iperf_cfg.addr);
    }

    ESP_FREE(iperf_data);
    g_iperf_cfg.finish = true;

    vTaskDelete(NULL);
}

static void espnow_iperf_responder_task(void *arg)
{
    esp_err_t ret         = ESP_OK;
    espnow_iperf_data_t *iperf_data   = ESP_MALLOC(ESPNOW_DATA_LEN);
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    size_t recv_size    = 0;
    uint32_t start_time = 0;
    uint32_t recv_count = 0;

    ESP_LOGI(TAG, "[  Initiator MAC  ] Interval       Transfer     Bandwidth   RSSI");

    for (uint32_t report_time = 0, report_count = 0;
            !g_iperf_cfg.finish;) {
        ret = espnow_recv(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          &recv_size, &rx_ctrl, pdMS_TO_TICKS(1000));
        ESP_ERROR_CONTINUE(ret == ESP_ERR_TIMEOUT, "");
        ESP_ERROR_BREAK(ret != ESP_OK, "<%s> espnow_recv", esp_err_to_name(ret));

        recv_count++;

        if (iperf_data->seq == 0) {
            recv_count   = 0;
            start_time  = esp_timer_get_time();
            report_time = start_time + g_iperf_cfg.report_interval * 1000 * 1000;
        }

        if (iperf_data->type == IPERF_BANDWIDTH && esp_timer_get_time() >= report_time) {
            uint32_t report_time_s = (report_time - start_time) / (1000 * 1000);
            double report_size    = (iperf_data->seq - report_count) * g_iperf_cfg.packet_len / 1e6;
            ESP_LOGI(TAG, "["MACSTR"]  %2d-%2d sec  %2.2f MBytes  %0.2f Mbits/sec  %d dbm",
                     MAC2STR(g_iperf_cfg.addr), report_time_s - g_iperf_cfg.report_interval, report_time_s,
                     report_size, report_size * 8 / g_iperf_cfg.report_interval, rx_ctrl.rssi);

            report_time = esp_timer_get_time() + g_iperf_cfg.report_interval * 1000 * 1000;
            report_count = iperf_data->seq;
        } else if (iperf_data->type == IPERF_PING) {
            ESP_LOGV(TAG, "Recv IPERF_PING, seq: %d, recv_count: %d", iperf_data->seq, recv_count);
            iperf_data->seq = recv_count;
            iperf_data->type = IPERF_PING_ACK;

            if (!g_iperf_cfg.frame_head.broadcast) {
                espnow_add_peer(g_iperf_cfg.addr, NULL);
            }

            ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                              sizeof(espnow_iperf_data_t), &g_iperf_cfg.frame_head, portMAX_DELAY);

            if (!g_iperf_cfg.frame_head.broadcast) {
                espnow_del_peer(g_iperf_cfg.addr);
            }

            ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));
        } else if (iperf_data->type == IPERF_BANDWIDTH_STOP) {
            uint32_t total_count = iperf_data->seq + 1;
            uint32_t lost_count  = total_count - recv_count;
            double total_len     = (total_count * g_iperf_cfg.packet_len) / 1e6;
            uint32_t spend_time_ms  = (esp_timer_get_time() - start_time) / 1000;

            ESP_LOGI(TAG, "[ ID] Interval      Transfer       Bandwidth      Jitter   Lost/Total Datagrams");
            ESP_LOGI(TAG, "[000] %2d-%2d sec    %2.2f MBytes    %0.2f Mbits/sec    %0.2f ms    %d/%d (%0.2f%%)",
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
            ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

            espnow_del_peer(g_iperf_cfg.addr);
        }
    }

    ESP_FREE(iperf_data);
    vTaskDelete(NULL);
}

static void espnow_iperf_ping_task(void *arg)
{
    esp_err_t ret     = ESP_OK;
    espnow_iperf_data_t *iperf_data = ESP_CALLOC(1, ESPNOW_DATA_LEN);
    size_t send_count = 0;
    size_t recv_count = 0;
    uint32_t spend_time_ms = 0;

    size_t recv_size      = 0;
    uint32_t max_time_ms  = 0;
    uint32_t min_time_ms  = UINT32_MAX;
    uint32_t res_time_ms  = 0;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_add_peer(g_iperf_cfg.addr, NULL);
    }

    for (int seq = 0; seq < g_iperf_cfg.ping_count && !g_iperf_cfg.finish; ++seq) {
        iperf_data->seq = recv_count;
        iperf_data->type = IPERF_PING;
        send_count++;

        uint32_t start_time = esp_timer_get_time();
        ret = espnow_send(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data,
                          sizeof(espnow_iperf_data_t), &g_iperf_cfg.frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        do {
            ret = espnow_recv(g_iperf_cfg.type, g_iperf_cfg.addr, iperf_data, &recv_size, &rx_ctrl, pdMS_TO_TICKS(1000));

            if (ret == ESP_OK && (iperf_data->type != IPERF_PING_ACK || iperf_data->seq != seq)) {
                ESP_LOGW(TAG, "data_size: %d", recv_size);
            }
        } while (ret == ESP_OK && (iperf_data->type != IPERF_PING_ACK || iperf_data->seq != seq));

        res_time_ms = (esp_timer_get_time() - start_time) / 1000;
        spend_time_ms += res_time_ms;

        max_time_ms = MAX(max_time_ms, res_time_ms);
        min_time_ms = MIN(min_time_ms, res_time_ms);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "seq=%d Destination Host Unreachable", iperf_data->seq);
            continue;
        }

        recv_count++;
        ESP_LOGI(TAG, "%d bytes from " MACSTR ": seq=%d rssi=%d time=%d ms",
                 recv_size, MAC2STR(g_iperf_cfg.addr), iperf_data->seq, rx_ctrl.rssi, res_time_ms);

        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_recv", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "initiator ping report:");
    ESP_LOGI(TAG, "ping statistics for " MACSTR, MAC2STR(g_iperf_cfg.addr));
    ESP_LOGI(TAG, "%d packets transmitted, %d received, %0.2f%% packet loss, time: total %d ms, max: %d, min: %d, average %0.2f ms",
             send_count, recv_count, (send_count - recv_count) * 100.0 / send_count,
             spend_time_ms, max_time_ms, min_time_ms, spend_time_ms * 1.0 / send_count);

    g_iperf_cfg.finish = true;

    if (!g_iperf_cfg.frame_head.broadcast) {
        espnow_del_peer(g_iperf_cfg.addr);
    }

    ESP_FREE(iperf_data);
    vTaskDelete(NULL);
}

static struct {
    struct arg_str *initiator;
    struct arg_lit *responder;
    struct arg_lit *ping;
    struct arg_int *interval;
    struct arg_int *len;
    struct arg_int *time;
    struct arg_int *broadcast;
    struct arg_lit *group;
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
    }

    if (espnow_iperf_args.interval->count) {
        g_iperf_cfg.report_interval = espnow_iperf_args.interval->ival[0];
    }

    if (espnow_iperf_args.time->count) {
        g_iperf_cfg.transmit_time = espnow_iperf_args.time->ival[0];
    }

    if (espnow_iperf_args.group->count) {
        g_iperf_cfg.frame_head.group = true;
    }

    if (espnow_iperf_args.broadcast->count) {
        g_iperf_cfg.frame_head.broadcast        = true;
        g_iperf_cfg.frame_head.retransmit_count = espnow_iperf_args.broadcast->ival[0];
    }

    uint8_t sta_mac[ESPNOW_ADDR_LEN] = {0};
    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);

    uint8_t channel                = 1;
    wifi_second_chan_t second      = 0;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&channel, &second));

    g_iperf_cfg.finish = false;

    espnow_set_qsize(ESPNOW_TYPE_RESERVED, 16);
    // esp_wifi_config_espnow_rate(ESP_IF_WIFI_STA, WIFI_PHY_RATE_MCS7_SGI);

    if (espnow_iperf_args.initiator->count) {
        ESP_ERROR_RETURN(!mac_str2hex(espnow_iperf_args.initiator->sval[0], g_iperf_cfg.addr), ESP_ERR_INVALID_ARG,
                         "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");

        ESP_LOGI(TAG, "------------------------------------------------------------");
        ESP_LOGI(TAG, "initiator " MACSTR " send to " MACSTR, MAC2STR(sta_mac), MAC2STR(g_iperf_cfg.addr));
        ESP_LOGI(TAG, "espnow channel: %d", channel);
        ESP_LOGI(TAG, "------------------------------------------------------------");
        ESP_LOGI(TAG, "time: %d, interval: %d, len: %d", g_iperf_cfg.transmit_time, g_iperf_cfg.report_interval,
                 g_iperf_cfg.packet_len);

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
        xTaskCreate(espnow_iperf_responder_task, "espnow_iperf_responder", 4 * 1024,
                    NULL, tskIDLE_PRIORITY + 1, NULL);
    }

    return ESP_OK;
}

void register_espnow_iperf()
{
    espnow_iperf_args.initiator = arg_str0("c", "initiator", "<responder (xx:xx:xx:xx:xx:xx)>", "run in initiator mode, ping to <responder>");
    espnow_iperf_args.responder = arg_lit0("s", "responder", "run in responder mode, receive from throughput or ping");
    espnow_iperf_args.ping      = arg_lit0("p", "ping", "run in ping mode, send to <responder>");
    espnow_iperf_args.interval  = arg_int0("i", "interval", "<interval (sec)>", "seconds between periodic bandwidth reports (default 3 secs)");
    espnow_iperf_args.time      = arg_int0("t", "time", "<time (sec)>", "time in seconds to transmit for (default 10 secs)");
    espnow_iperf_args.len       = arg_int0("l", "len", "<len (Bytes)>", "length of buffer in bytes to read or write (Defaults: 230 Bytes)");
    espnow_iperf_args.broadcast = arg_int0("b", "broadcast", "<count>", "Send package by broadcast");
    espnow_iperf_args.group     = arg_lit0("g", "group", "Send a package to a group");
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
