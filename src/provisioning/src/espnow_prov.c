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
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "espnow.h"
#include "espnow_prov.h"
#include "espnow_utils.h"

typedef enum {
    ESPNOW_PROV_TYPE_BEACON,
    ESPNOW_PROV_TYPE_DEVICE,
    ESPNOW_PROV_TYPE_WIFI,
} espnow_prov_type_t;

typedef struct {
    uint8_t type;
    union {
        espnow_prov_initiator_t initiator_info;
        espnow_prov_responder_t responder_info;
        espnow_prov_wifi_t wifi_config;
    };
} __attribute__((packed)) espnow_prov_data_t;

#define ESPNOW_PROV_BEACON_INTERVAL 100

#ifdef CONFIG_ESPNOW_ALL_SECURITY
#define CONFIG_ESPNOW_PROV_SECURITY 1
#else
#ifndef CONFIG_ESPNOW_PROV_SECURITY
#define CONFIG_ESPNOW_PROV_SECURITY 0
#endif
#endif

static const char *TAG = "espnow_prov";

/*
 * Concurrency: espnow_prov_recv() runs in the ESP-NOW task and may still be
 * executing after the handler is unregistered. The contexts are therefore
 * allocated once and never freed. The scan flags are enabled/disabled with
 * release/acquire ordering (enable last on start, disable first on stop); the
 * initiator request and the responder session are published and closed under
 * g_prov_init_lock and g_prov_resp_lock.
 */
#define PROV_FLAG_SET(flag, val)  __atomic_store_n(&(flag), (val), __ATOMIC_RELEASE)
#define PROV_FLAG_GET(flag)       __atomic_load_n(&(flag), __ATOMIC_ACQUIRE)

typedef struct {
    bool beacon_en;
    bool wifi_en;
    bool fix_ch;
    bool config;
    espnow_prov_cb_t wifi_cb;
    /* Incremented by every espnow_prov_initiator_send(). The context is reused
     * by later calls, so a WIFI reply is counted only for the call whose
     * callback accepted it: a callback that is still running when its call
     * ends must not complete the next call. */
    uint32_t session;
    uint32_t stop_tick;
    struct {                        /* BEACON result */
        espnow_prov_responder_t responder_info;
        wifi_pkt_rx_ctrl_t rx_ctrl;
        uint8_t addr[ESPNOW_ADDR_LEN];
    } scan_info;
} espnow_prov_init_t;

typedef struct {
    bool device_en;
    espnow_prov_cb_t device_cb;
    espnow_prov_wifi_t *wifi_config;  /* ESPNOW_PROV_WIFI_CONFIG_MAX_SIZE bytes */
    /* Incremented by every espnow_prov_responder_start(). A DEVICE request is
     * answered only if the session that authenticated it is still the current
     * one, so an authorization given by one session's callback can never send
     * the credentials of a later session. */
    uint32_t session;
} espnow_prov_resp_t;

/* custom_size is a uint8_t, so this holds any wifi_config the API accepts. */
#define ESPNOW_PROV_WIFI_CONFIG_MAX_SIZE  (sizeof(espnow_prov_wifi_t) + UINT8_MAX)

static espnow_prov_init_t *g_prov_init = NULL;
static espnow_prov_resp_t *g_prov_resp = NULL;
/* Guards the responder session state: credentials, callback, session number,
 * device_en, and what espnow_prov_responder_start() shares with the beacon
 * timer (stop tick, beacon content, timer handle), see responder_beacon_timercb().
 * espnow_prov_recv() reads the session identity under it before calling the
 * callback, and espnow_prov_responder_send() checks that identity again and
 * copies the credentials under it, so a WIFI frame always carries the complete
 * credentials of the session whose callback approved the request, or is not
 * sent. Only non-blocking work is done while it is held; the application
 * callback, allocations and sends run outside. */
static portMUX_TYPE g_prov_resp_lock = portMUX_INITIALIZER_UNLOCKED;
/* Guards the initiator request state of espnow_prov_initiator_send(): wifi_en,
 * the callback, the request number and the result. espnow_prov_recv() reads the
 * request's identity under it before calling the callback and, after the
 * callback has accepted the reply, records the result only if that request is
 * still open; espnow_prov_initiator_send() closes the request and reads its
 * result in one step (the wait loop only polls the result flag, without the
 * lock). Only non-blocking work is done while it is held; the application
 * callback runs outside. */
static portMUX_TYPE g_prov_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t espnow_prov_responder_send(const espnow_addr_t *initiator_addr_list, size_t initiator_addr_num,
                                     uint32_t session);

static esp_err_t espnow_prov_recv(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    uint8_t data_type = ((uint8_t *)data)[0];
    espnow_prov_data_t *recv_data = (espnow_prov_data_t *)data;

    switch (data_type) {
        case ESPNOW_PROV_TYPE_BEACON:
            ESP_LOGD(TAG, "ESPNOW_PROV_TYPE_BEACON");
            if (g_prov_init && PROV_FLAG_GET(g_prov_init->beacon_en) && !PROV_FLAG_GET(g_prov_init->fix_ch)) {
                if (size < 1 + sizeof(espnow_prov_responder_t)) {
                    ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Drop PROV BEACON frame too short: %u", (unsigned)size);
                    break;
                }
                /* Write once, then publish via fix_ch. */
                memcpy(&g_prov_init->scan_info.responder_info, &recv_data->responder_info, sizeof(espnow_prov_responder_t));
                memcpy(&g_prov_init->scan_info.rx_ctrl, rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
                memcpy(g_prov_init->scan_info.addr, src_addr, ESPNOW_ADDR_LEN);
                PROV_FLAG_SET(g_prov_init->fix_ch, true);
            }
            break;

        case ESPNOW_PROV_TYPE_DEVICE: {
            ESP_LOGD(TAG, "ESPNOW_PROV_TYPE_DEVICE");
            if (!g_prov_resp) {
                break;
            }
            /* Take the session's identity in one step: enabled, callback and
             * session number belong together. cb is the only authentication
             * point. Without one every request is approved (legacy behaviour,
             * see espnow_prov_responder_start). */
            bool device_en;
            uint32_t session;
            espnow_prov_cb_t device_cb;
            portENTER_CRITICAL(&g_prov_resp_lock);
            device_en = g_prov_resp->device_en;
            device_cb = g_prov_resp->device_cb;
            session   = g_prov_resp->session;
            portEXIT_CRITICAL(&g_prov_resp_lock);
            if (!device_en) {
                break;
            }
            /* The callback reads the fixed fields plus custom_data[custom_size]. */
            if (size < 1 + sizeof(espnow_prov_initiator_t)
                    || size < 1 + sizeof(espnow_prov_initiator_t) + recv_data->initiator_info.custom_size) {
                ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Drop PROV DEVICE frame too short: %u", (unsigned)size);
                break;
            }
            ret = ESP_OK;
            if (device_cb) {
                ret = device_cb(src_addr, &recv_data->initiator_info, size - 1, rx_ctrl);
            }
            if (ret == ESP_OK) {
                espnow_addr_t initiator_addr = {0};
                memcpy(initiator_addr, src_addr, 6);
                /* The reply carries the credentials of `session` or nothing: if
                 * the responder was restarted or expired while the callback
                 * ran, this authorization no longer applies. */
                ret = espnow_prov_responder_send((const espnow_addr_t *)&initiator_addr, 1, session);
            }
            break;
        }

        case ESPNOW_PROV_TYPE_WIFI: {
            ESP_LOGD(TAG, "ESPNOW_PROV_TYPE_WIFI");
            if (!g_prov_init) {
                break;
            }
            /* Take the request's identity in one step: enabled, callback and
             * request number belong together. */
            bool wifi_en;
            uint32_t session;
            espnow_prov_cb_t wifi_cb;
            portENTER_CRITICAL(&g_prov_init_lock);
            wifi_en = g_prov_init->wifi_en;
            wifi_cb = g_prov_init->wifi_cb;
            session = g_prov_init->session;
            portEXIT_CRITICAL(&g_prov_init_lock);
            if (!wifi_en) {
                break;
            }
            /* The reply is accepted from any address, not only the responder the
             * DEVICE request went to (legacy behaviour, see espnow_prov_initiator_send).
             * With CONFIG_ESPNOW_PROV_SECURITY only a key holder can produce it. */
            if (size < 1 + sizeof(espnow_prov_wifi_t)
                    || size < 1 + sizeof(espnow_prov_wifi_t) + recv_data->wifi_config.custom_size) {
                ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Drop PROV WIFI frame too short: %u", (unsigned)size);
                break;
            }
            ret = ESP_OK;
            if (wifi_cb) {
                ret = wifi_cb(src_addr, &recv_data->wifi_config, size - 1, rx_ctrl);
            }
            if (ret == ESP_OK) {
                /* The result belongs to the request whose callback accepted the
                 * reply. If that request timed out (or a new one was started)
                 * while the callback ran, it is not recorded: the context is
                 * reused, and the next request must wait for a reply accepted by
                 * its own callback. */
                bool current;
                portENTER_CRITICAL(&g_prov_init_lock);
                current = g_prov_init->wifi_en && g_prov_init->session == session;
                if (current) {
                    PROV_FLAG_SET(g_prov_init->config, true);
                }
                portEXIT_CRITICAL(&g_prov_init_lock);
                if (!current) {
                    ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Initiator request ended while the WIFI reply from " MACSTR " was being checked, result discarded",
                                            MAC2STR(src_addr));
                    ret = ESP_ERR_INVALID_STATE;
                }
            }
            break;
        }

        default:
            break;
    }

    return ret;
}

esp_err_t espnow_prov_initiator_scan(espnow_addr_t responder_addr, espnow_prov_responder_t *responder_info,
                                    wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_addr);
    ESP_PARAM_CHECK(responder_info);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret                 = ESP_FAIL;

    wifi_country_t country = {0};
    uint32_t start_ticks = xTaskGetTickCount();

    if (!g_prov_init) {
        g_prov_init = ESP_CALLOC(1, sizeof(espnow_prov_init_t));
        ESP_ERROR_RETURN(!g_prov_init, ESP_ERR_NO_MEM, "ESP_CALLOC g_prov_init");
    }

    /* Clear the result flag before enabling the handler. */
    PROV_FLAG_SET(g_prov_init->fix_ch, false);
    PROV_FLAG_SET(g_prov_init->beacon_en, true);

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 1, espnow_prov_recv);
    esp_wifi_get_country(&country);

    while (!PROV_FLAG_GET(g_prov_init->fix_ch) && (wait_ticks == portMAX_DELAY || xTaskGetTickCount() - start_ticks < wait_ticks)) {
        for (int i = 0; i < country.nchan && !PROV_FLAG_GET(g_prov_init->fix_ch); ++i) {
            esp_wifi_set_channel(country.schan + i, WIFI_SECOND_CHAN_NONE);
            ESP_LOGD(TAG, "espnow_send, channel: %d", country.schan + i);
            vTaskDelay(pdMS_TO_TICKS(ESPNOW_PROV_BEACON_INTERVAL + 10));
        }
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);
    PROV_FLAG_SET(g_prov_init->beacon_en, false);

    /* Copy out in the caller's task; the handler stops writing once fix_ch is set. */
    if (PROV_FLAG_GET(g_prov_init->fix_ch)) {
        memcpy(responder_info, &g_prov_init->scan_info.responder_info, sizeof(espnow_prov_responder_t));
        memcpy(rx_ctrl, &g_prov_init->scan_info.rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
        memcpy(responder_addr, g_prov_init->scan_info.addr, ESPNOW_ADDR_LEN);
        ret = ESP_OK;
    }

    return ret;
}

esp_err_t espnow_prov_initiator_send(const espnow_addr_t responder_addr, const espnow_prov_initiator_t *initiator_info, espnow_prov_cb_t cb, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(responder_addr);
    ESP_PARAM_CHECK(initiator_info);

    esp_err_t ret = ESP_ERR_WIFI_TIMEOUT;
    /* Send only the initialized prefix; the full union would leak heap. */
    size_t size = 1 + sizeof(espnow_prov_initiator_t) + initiator_info->custom_size;

    if (!g_prov_init) {
        g_prov_init = ESP_CALLOC(1, sizeof(espnow_prov_init_t));
        ESP_ERROR_RETURN(!g_prov_init, ESP_ERR_NO_MEM, "ESP_CALLOC g_prov_init");
    }

    espnow_prov_data_t *prov_data = ESP_MALLOC(size);
    ESP_ERROR_RETURN(!prov_data, ESP_ERR_NO_MEM, "ESP_MALLOC prov_data");
    prov_data->type = ESPNOW_PROV_TYPE_DEVICE;
    memcpy(&prov_data->initiator_info, initiator_info, size - 1);
    espnow_frame_head_t frame_head = {
        .filter_adjacent_channel = true,
        .security                = CONFIG_ESPNOW_PROV_SECURITY,
    };
    uint32_t start_ticks = xTaskGetTickCount();

    /* Arm the receiver before the request goes out. The responder answers as
     * soon as it has processed the DEVICE frame, and this task may be preempted
     * between espnow_send() returning and the next statement, so a reply that
     * arrives before the handler is registered is silently dropped in
     * espnow_recv_cb() and the caller only sees the timeout. The request is
     * published in one step under the lock (new request number, callback,
     * cleared result, enabled): a callback of an earlier request that is still
     * running can then no longer record its result, see espnow_prov_recv(). */
    portENTER_CRITICAL(&g_prov_init_lock);
    g_prov_init->session++;
    g_prov_init->wifi_cb = cb;
    PROV_FLAG_SET(g_prov_init->config, false);
    g_prov_init->wifi_en = true;
    portEXIT_CRITICAL(&g_prov_init_lock);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 1, espnow_prov_recv);

    espnow_add_peer(responder_addr, NULL);

    ret = espnow_send(ESPNOW_DATA_TYPE_PROV, responder_addr, prov_data,
                      size, &frame_head, portMAX_DELAY);

    espnow_del_peer(responder_addr);
    ESP_FREE(prov_data);

    if (ret != ESP_OK) {
        /* Nothing was sent, so nothing can be answered: disarm again. This
         * also closes the request for a callback that is running right now. */
        espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);
        portENTER_CRITICAL(&g_prov_init_lock);
        g_prov_init->wifi_en = false;
        portEXIT_CRITICAL(&g_prov_init_lock);
        ESP_ERROR_RETURN(true, ret, "espnow_write");
    }

    while (!PROV_FLAG_GET(g_prov_init->config) && (wait_ticks == portMAX_DELAY || xTaskGetTickCount() - start_ticks < wait_ticks)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);

    /* Close the request and read its result in one step. A callback that
     * accepted a reply before this point has recorded it and the call succeeds;
     * one that returns later finds the request closed and records nothing, so
     * it cannot complete a following call either. Keep the context: a frame may
     * still be in the handler. */
    bool done;
    portENTER_CRITICAL(&g_prov_init_lock);
    g_prov_init->wifi_en = false;
    done = g_prov_init->config;
    portEXIT_CRITICAL(&g_prov_init_lock);
    ret = done ? ESP_OK : ESP_FAIL;

    return ret;
}

/* Responder session state, all guarded by g_prov_resp_lock. g_beacon_timer is
 * the timer of the current session (NULL: no session). The beacon buffer is
 * allocated once and never freed, like the other contexts, so a restart that
 * rewrites it can never race a free in the timer task. */
static uint32_t g_beacon_stop_tick = 0;
static TimerHandle_t g_beacon_timer = NULL;
static espnow_prov_data_t *g_beacon_prov_data = NULL;

/* The part of espnow_prov_data_t a beacon carries. The timer service task has
 * a small stack, so only this prefix is copied there, not the whole union. */
typedef struct {
    uint8_t type;
    espnow_prov_responder_t responder_info;
} __attribute__((packed)) espnow_prov_beacon_t;

static void responder_beacon_timercb(TimerHandle_t timer)
{
    espnow_prov_beacon_t beacon;
    bool current;
    bool expired = false;

    /* The expiry decision and the state change it implies are made under the
     * lock, so a concurrent espnow_prov_responder_start() either runs before it
     * (and extends this session, which then stays up) or after it (and starts a
     * new session with a new timer, which this callback never touches). A timer
     * that is not the current one has already been torn down and is only waiting
     * for its delete command to be processed. */
    portENTER_CRITICAL(&g_prov_resp_lock);
    current = (timer == g_beacon_timer);
    if (current) {
        expired = (g_beacon_stop_tick < xTaskGetTickCount());
        if (expired) {
            g_beacon_timer = NULL;
            /* Disable only; freeing here would race espnow_prov_recv() in the RX
             * task. Keep device_cb as well: a DEVICE frame that already passed
             * the device_en check must still go through the caller's callback,
             * not the "NULL approves" path. The next start overwrites it. */
            if (g_prov_resp) {
                PROV_FLAG_SET(g_prov_resp->device_en, false);
            }
            /* Two assignments in espnow.c, nothing blocking. Done under the lock
             * so a new start cannot register the handler in between and lose it. */
            espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 0, NULL);
        } else {
            memcpy(&beacon, g_beacon_prov_data, sizeof(beacon));
        }
    }
    portEXIT_CRITICAL(&g_prov_resp_lock);

    if (!current) {
        return;
    }

    if (expired) {
        /* Timer commands only, outside the lock; this timer is owned by nobody
         * else any more. */
        vTimerSetReloadMode(timer, pdFALSE);
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
        ESP_LOGI(TAG, "Responder beacon end");
        return;
    }

    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .magic     = esp_random(),
        .filter_adjacent_channel = true,
        .security                = CONFIG_ESPNOW_PROV_SECURITY,
    };

    espnow_send(ESPNOW_DATA_TYPE_PROV, ESPNOW_ADDR_BROADCAST, &beacon,
                sizeof(beacon), &frame_head, portMAX_DELAY);
}

esp_err_t espnow_prov_responder_start(const espnow_prov_responder_t *responder_info, TickType_t wait_ticks, 
                                        const espnow_prov_wifi_t *wifi_config, espnow_prov_cb_t cb)
{
    ESP_PARAM_CHECK(responder_info);
    ESP_PARAM_CHECK(wifi_config);

    /* Allocate the resident buffers first; a failure here changes nothing. All
     * of them are allocated once and reused by later starts. */
    if (!g_prov_resp) {
        g_prov_resp = ESP_CALLOC(1, sizeof(espnow_prov_resp_t));
        ESP_ERROR_RETURN(!g_prov_resp, ESP_ERR_NO_MEM, "ESP_CALLOC g_prov_resp");
    }

    /* The buffer is allocated once at the maximum size and never freed: a
     * previous DEVICE frame may still be sending from it in the ESP-NOW task. */
    if (!g_prov_resp->wifi_config) {
        g_prov_resp->wifi_config = ESP_CALLOC(1, ESPNOW_PROV_WIFI_CONFIG_MAX_SIZE);
        ESP_ERROR_RETURN(!g_prov_resp->wifi_config, ESP_ERR_NO_MEM, "ESP_CALLOC wifi_config");
    }

    if (!g_beacon_prov_data) {
        g_beacon_prov_data = ESP_MALLOC(sizeof(espnow_prov_responder_t) + 1);
        ESP_ERROR_RETURN(!g_beacon_prov_data, ESP_ERR_NO_MEM, "ESP_MALLOC g_beacon_prov_data");
    }

    /* Publish the whole session in one step under the lock, so the beacon timer
     * can neither see a half-updated session nor tear this one down between its
     * expiry check and its cleanup (see responder_beacon_timercb()). A timer is
     * needed only when no session is running; it is created and started outside
     * the lock and installed inside it. A callback that fires before the install
     * finds it is not the current timer and does nothing. If the running session
     * ends between the check and the install, the loop creates the timer then;
     * if one appears in the meantime, the timer created here is a spare. */
    TimerHandle_t timer = NULL;

    for (;;) {
        portENTER_CRITICAL(&g_prov_resp_lock);
        if (g_beacon_timer || timer) {
            if (!g_beacon_timer) {
                g_beacon_timer = timer;
                timer = NULL;
            }
            g_beacon_stop_tick = xTaskGetTickCount() + wait_ticks;
            g_beacon_prov_data->type = ESPNOW_PROV_TYPE_BEACON;
            memcpy(&g_beacon_prov_data->responder_info, responder_info, sizeof(espnow_prov_responder_t));
            /* Store the full fixed + custom_size length. */
            memcpy(g_prov_resp->wifi_config, wifi_config, sizeof(espnow_prov_wifi_t) + wifi_config->custom_size);
            /* New session: a DEVICE request authenticated by the previous
             * callback must not be answered with these credentials. */
            g_prov_resp->session++;
            /* Enable last, so the handler sees a complete context. */
            __atomic_store_n(&g_prov_resp->device_cb, cb, __ATOMIC_RELEASE);
            PROV_FLAG_SET(g_prov_resp->device_en, true);
            portEXIT_CRITICAL(&g_prov_resp_lock);
            break;
        }
        portEXIT_CRITICAL(&g_prov_resp_lock);

        /* Nothing has been changed yet, so a failure here needs no rollback. */
        timer = xTimerCreate("responder_beacon",
                             pdMS_TO_TICKS(ESPNOW_PROV_BEACON_INTERVAL),
                             true, NULL, responder_beacon_timercb);
        if (!timer || xTimerStart(timer, 0) != pdPASS) {
            if (timer) {
                xTimerDelete(timer, 0);
            }
            ESP_LOGW(TAG, "[%s, %d] beacon timer create / start failed", __func__, __LINE__);
            return ESP_ERR_NO_MEM;
        }
    }

    if (timer) {
        /* Spare: another session was published while it was being created. */
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
    }

    ESP_LOGI(TAG, "Responder beacon start, timer: %ds", pdTICKS_TO_MS(wait_ticks) / 1000);

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_PROV, 1, espnow_prov_recv);

    return ESP_OK;
}

/**
 * @brief  Responder sends the WiFi configuration of one session
 *
 * @param[in]  initiator_addr_list  mac address list of initiators
 * @param[in]  initiator_addr_num  initiator address number
 * @param[in]  session  the session whose callback authorized this reply
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 *    - ESP_ERR_INVALID_STATE: `session` is no longer the current, enabled
 *      session (restarted or expired while the request was authenticated);
 *      nothing is sent
 *    - ESP_ERR_NO_MEM
 */
static esp_err_t espnow_prov_responder_send(const espnow_addr_t *initiator_addr_list, size_t initiator_addr_num,
                                     uint32_t session)
{
    ESP_PARAM_CHECK(initiator_addr_list);

    esp_err_t ret = ESP_OK;
    bool peer_added = false;
    bool group_added = false;
    bool current;
    size_t size = 0;
    uint8_t dest_addr[6] = {0};
    espnow_prov_data_t *prov_data = NULL;
    espnow_frame_head_t frame_head = {
        .retransmit_count = 10,
        .broadcast = true,
        .security  = CONFIG_ESPNOW_PROV_SECURITY,
    };

    ESP_LOGD(TAG, MACSTR ", num: %d", MAC2STR(initiator_addr_list[0]), initiator_addr_num);

    /* Allocate for the maximum size, then, under the lock, check that the
     * authorizing session is still the current one and copy its credentials.
     * The shared buffer may be rewritten by a concurrent
     * espnow_prov_responder_start(); after that the callback's verdict does not
     * apply to the new credentials. Reachable in response to a remote DEVICE
     * frame; do not memcpy into a failed allocation (CWE-476). */
    prov_data = ESP_MALLOC(1 + ESPNOW_PROV_WIFI_CONFIG_MAX_SIZE);
    if (!prov_data) {
        ESP_LOGW(TAG, "[%s, %d] ESP_MALLOC prov_data", __func__, __LINE__);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&g_prov_resp_lock);
    current = g_prov_resp && g_prov_resp->wifi_config && g_prov_resp->device_en
              && g_prov_resp->session == session;
    if (current) {
        size = 1 + sizeof(espnow_prov_wifi_t) + g_prov_resp->wifi_config->custom_size;
        memcpy(&prov_data->wifi_config, g_prov_resp->wifi_config, size - 1);
    }
    portEXIT_CRITICAL(&g_prov_resp_lock);
    if (!current) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Responder session changed while authenticating " MACSTR ", no WIFI reply",
                                MAC2STR(initiator_addr_list[0]));
        ESP_FREE(prov_data);
        return ESP_ERR_INVALID_STATE;
    }
    prov_data->type = ESPNOW_PROV_TYPE_WIFI;

    if (initiator_addr_num > 1) {
        espnow_set_group(initiator_addr_list, initiator_addr_num, ESPNOW_ADDR_GROUP_PROV, NULL, true, portMAX_DELAY);
        group_added = true;
        frame_head.group = true;
        memcpy(dest_addr, ESPNOW_ADDR_GROUP_PROV, 6);
    } else {
        memcpy(dest_addr, initiator_addr_list[0], 6);
        /* Only remove on exit what this call added: the application may keep a
         * long-lived peer for this address. */
        if (!esp_now_is_peer_exist(dest_addr)) {
            espnow_add_peer(dest_addr, NULL);
            peer_added = true;
        }
    }

    ret = espnow_send(ESPNOW_DATA_TYPE_PROV, dest_addr, prov_data, size, &frame_head, portMAX_DELAY);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[%s, %d] <%s> " "espnow_send", __func__, __LINE__, esp_err_to_name(ret));
    }
    ESP_FREE(prov_data);

    /* Single exit: the group / peer added above is removed on the send failure
     * and success paths alike. */
    if (group_added) {
        espnow_set_group(initiator_addr_list, initiator_addr_num, ESPNOW_ADDR_GROUP_PROV, NULL, false, portMAX_DELAY);
    } else if (peer_added) {
        espnow_del_peer(dest_addr);
    }

    return ret;
}
