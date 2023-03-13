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
#include <string.h>

#include "sdkconfig.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_console.h"
#include "esp_event.h"
#include "esp_log.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "esp_wifi.h"

#include "espnow_utils.h"

#include "nvs_flash.h"
#include "argtable3/argtable3.h"
#include "ping/ping_sock.h"

static const char *TAG = "wifi_cmd";

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
    wifi_ap_record_t *ap_record    = NULL;
    wifi_scan_config_t scan_config = {
        .show_hidden = true,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };

    if (wifi_scan_args.passive->count) {
        scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        scan_config.scan_time.passive = wifi_scan_args.passive->ival[0];
    }

    if (wifi_scan_args.ssid->count) {
        scan_config.ssid = (uint8_t *)wifi_scan_args.ssid->sval[0];
    }

    if (wifi_scan_args.bssid->count) {
        ESP_ERROR_RETURN(!espnow_mac_str2hex(wifi_scan_args.bssid->sval[0], bssid), ESP_ERR_INVALID_ARG,
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
    struct arg_str *country_code;
    struct arg_int *channel;
    struct arg_str *ssid;
    struct arg_str *bssid;
    struct arg_str *password;
    struct arg_int *tx_power;
    struct arg_lit *info;
    struct arg_lit *disconnt;
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
        ESP_ERROR_RETURN(!espnow_mac_str2hex(wifi_config_args.bssid->sval[0], wifi_config.sta.bssid), ESP_ERR_INVALID_ARG,
                         "The format of the address is incorrect. Please enter the format as xx:xx:xx:xx:xx:xx");
    }

    if (wifi_config_args.disconnt->count) {
        ret = esp_wifi_disconnect();
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_wifi_disconnect");
    }

    if (strlen((char *)wifi_config.sta.ssid)) {
        ret = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_wifi_set_config");

        ret = esp_wifi_connect();
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_wifi_connect");
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

        extern wifi_pkt_rx_ctrl_t g_rx_ctrl;

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
    wifi_config_args.disconnt = arg_lit0("d", "disconnect", "Disconnect with router");
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

static void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    ESP_LOGI(TAG, "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms",
           recv_len, ipaddr_ntoa((ip_addr_t*)&target_addr), seqno, ttl, elapsed_time);
}

static void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    ESP_LOGI(TAG, "From %s icmp_seq=%d timeout",ipaddr_ntoa((ip_addr_t*)&target_addr), seqno);
}

static void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    ip_addr_t target_addr;
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    uint32_t loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
    if (IP_IS_V4(&target_addr)) {
        ESP_LOGI(TAG, "\n--- %s ping statistics ---", inet_ntoa(*ip_2_ip4(&target_addr)));
#if CONFIG_LWIP_IPV6
    } else {
        ESP_LOGI(TAG, "\n--- %s ping statistics ---", inet6_ntoa(*ip_2_ip6(&target_addr)));
#endif
    }
    ESP_LOGI(TAG, "%d packets transmitted, %d received, %d%% packet loss, time %dms",
           transmitted, received, loss, total_time_ms);
    // delete the ping sessions, so that we clean up all resources and can create a new ping session
    // we don't have to call delete function in the callback, instead we can call delete function from other tasks
    esp_ping_delete_session(hdl);
}

static struct {
    struct arg_dbl *timeout;
    struct arg_dbl *interval;
    struct arg_int *data_size;
    struct arg_int *count;
    struct arg_int *tos;
    struct arg_str *host;
    struct arg_end *end;
} ping_args;

static int do_ping_cmd(int argc, char **argv)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.task_stack_size = 3072;

    int nerrors = arg_parse(argc, argv, (void **)&ping_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ping_args.end, argv[0]);
        return 1;
    }

    if (ping_args.timeout->count > 0) {
        config.timeout_ms = (uint32_t)(ping_args.timeout->dval[0] * 1000);
    }

    if (ping_args.interval->count > 0) {
        config.interval_ms = (uint32_t)(ping_args.interval->dval[0] * 1000);
    }

    if (ping_args.data_size->count > 0) {
        config.data_size = (uint32_t)(ping_args.data_size->ival[0]);
    }

    if (ping_args.count->count > 0) {
        config.count = (uint32_t)(ping_args.count->ival[0]);
    }

    if (ping_args.tos->count > 0) {
        config.tos = (uint32_t)(ping_args.tos->ival[0]);
    }

    // parse IP address
    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));

#if CONFIG_LWIP_IPV6
    struct sockaddr_in6 sock_addr6;
    if (inet_pton(AF_INET6, ping_args.host->sval[0], &sock_addr6.sin6_addr) == 1) {
        /* convert ip6 string to ip6 address */
        ipaddr_aton(ping_args.host->sval[0], &target_addr);
    } else {
#else
    {
#endif
        struct addrinfo hint;
        struct addrinfo *res = NULL;
        memset(&hint, 0, sizeof(hint));
        /* convert ip4 string or hostname to ip4 or ip6 address */
        if (getaddrinfo(ping_args.host->sval[0], NULL, &hint, &res) != 0) {
            ESP_LOGI(TAG, "ping: unknown host %s", ping_args.host->sval[0]);
            return 1;
        }
        if (res->ai_family == AF_INET) {
            struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
            inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
#if CONFIG_LWIP_IPV6
        } else {
            struct in6_addr addr6 = ((struct sockaddr_in6 *) (res->ai_addr))->sin6_addr;
            inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6);
#endif
        }
        freeaddrinfo(res);
    }
    config.target_addr = target_addr;

    /* set callback functions */
    esp_ping_callbacks_t cbs = {
        .on_ping_success = cmd_ping_on_ping_success,
        .on_ping_timeout = cmd_ping_on_ping_timeout,
        .on_ping_end = cmd_ping_on_ping_end,
        .cb_args = NULL
    };
    esp_ping_handle_t ping;
    esp_ping_new_session(&config, &cbs, &ping);
    esp_ping_start(ping);

    return 0;
}

void register_wifi_ping(void)
{
    ping_args.timeout = arg_dbl0("W", "timeout", "<t>", "Time to wait for a response, in seconds");
    ping_args.interval = arg_dbl0("i", "interval", "<t>", "Wait interval seconds between sending each packet");
    ping_args.data_size = arg_int0("s", "size", "<n>", "Specify the number of data bytes to be sent");
    ping_args.count = arg_int0("c", "count", "<n>", "Stop after sending count packets");
    ping_args.tos = arg_int0("Q", "tos", "<n>", "Set Type of Service related bits in IP datagrams");
    ping_args.host = arg_str1(NULL, NULL, "<host>", "Host address");
    ping_args.end = arg_end(1);
    const esp_console_cmd_t ping_cmd = {
        .command = "ping",
        .help = "send ICMP ECHO_REQUEST to network hosts",
        .hint = NULL,
        .func = &do_ping_cmd,
        .argtable = &ping_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ping_cmd));
}

static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} ap_args;

static bool wifi_cmd_ap_set(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "",
            .ssid_len = 0,
            .max_connection = 4,
            .password = "",
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    strlcpy((char *) wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    if (pass) {
        if (strlen(pass) != 0 && strlen(pass) < 8) {
            ESP_LOGE(TAG, "password less than 8");
            return false;
        }
        strlcpy((char *) wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));
    }

    if (strlen(pass) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    return true;
}

static int wifi_cmd_ap(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &ap_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, ap_args.end, argv[0]);
        return 1;
    }

    wifi_cmd_ap_set(ap_args.ssid->sval[0], ap_args.password->sval[0]);
    ESP_LOGI(TAG, "AP mode, %s %s", ap_args.ssid->sval[0], ap_args.password->sval[0]);
    return 0;
}

void register_wifi_ap(void)
{
    ap_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    ap_args.password = arg_str0(NULL, NULL, "<pass>", "password of AP");
    ap_args.end = arg_end(2);

    const esp_console_cmd_t ap_cmd = {
        .command = "ap",
        .help = "AP mode, configure ssid and password",
        .hint = NULL,
        .func = &wifi_cmd_ap,
        .argtable = &ap_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&ap_cmd) );
}

void register_wifi(void)
{
    register_wifi_scan();
    register_wifi_config();
    register_wifi_ping();
    register_wifi_ap();
}