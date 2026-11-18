/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/param.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "espnow.h"
#include "espnow_time.h"
#include "espnow_mem.h"
#include "espnow_utils.h"

static const char *TAG = "espnow_time";

#define ESPNOW_TIME_VERSION         1
#define ESPNOW_TIME_TYPE_BROADCAST  0x01  /* Initiator broadcasts time */
#define ESPNOW_TIME_TYPE_REQUEST    0x02  /* Responder requests time */

#ifdef CONFIG_ESPNOW_ALL_SECURITY
#define CONFIG_ESPNOW_TIME_SECURITY 1
#else
#ifndef CONFIG_ESPNOW_TIME_SECURITY
#define CONFIG_ESPNOW_TIME_SECURITY 0
#endif
#endif

typedef enum {
    ESPNOW_TIME_ROLE_NONE = 0,
    ESPNOW_TIME_ROLE_INITIATOR,   /* Controller: broadcasts time */
    ESPNOW_TIME_ROLE_RESPONDER,   /* Data node: receives time */
} espnow_time_role_t;

static espnow_time_role_t g_time_role = ESPNOW_TIME_ROLE_NONE;
static espnow_time_initiator_config_t g_initiator_config;
static espnow_time_responder_config_t g_responder_config;
static TimerHandle_t g_initiator_timer = NULL;
static int64_t g_time_offset_us = 0;  /* Offset from initiator time */
/* Time (ms) of the last failed reply to a REQUEST; 0 when the last reply succeeded. */
static uint32_t g_req_fail_ms = 0;

/* After a reply to a REQUEST fails, REQUESTs are ignored for this long. */
#define ESPNOW_TIME_REQ_FAIL_HOLD_MS  1000

/**
 * @brief Send time packet
 *
 * @param type    packet type
 * @param remote  true when sending in reply to a received (unauthenticated) REQUEST:
 *                the failure log is rate-limited so a flood of REQUESTs cannot log
 *                per packet. Application and periodic broadcasts log every failure.
 */
static esp_err_t espnow_time_send_packet(uint8_t type, bool remote)
{
    espnow_time_packet_t pkt = {
        .version = ESPNOW_TIME_VERSION,
        .type = type,
        .timestamp_us = esp_timer_get_time() + g_time_offset_us,
        .utc_time_us = 0,
    };

    /* Get UTC time if available */
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0 && tv.tv_sec > 1577808000) {  /* After 2020-01-01 */
        pkt.utc_time_us = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    }

    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame_head.broadcast = true;
    frame_head.retransmit_count = 3;
#if CONFIG_ESPNOW_TIME_SECURITY
    frame_head.security = true;
#endif

    esp_err_t ret = espnow_send(ESPNOW_DATA_TYPE_TIMESYNC, ESPNOW_ADDR_BROADCAST,
                                 &pkt, sizeof(pkt), &frame_head, pdMS_TO_TICKS(1000));
    if (ret != ESP_OK) {
        if (remote) {
            ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Failed to send time packet: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGW(TAG, "Failed to send time packet: %s", esp_err_to_name(ret));
        }
    }

    return ret;
}

/**
 * @brief Handle received time synchronization packet
 */
static esp_err_t espnow_time_recv_handler(uint8_t *src_addr, void *data,
                                           size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);

    /* TIME frames may be plaintext; rate-limit remotely triggerable logs. */
    if (size < sizeof(espnow_time_packet_t)) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Invalid time packet size: %d", size);
        return ESP_ERR_INVALID_SIZE;
    }

    espnow_time_packet_t *pkt = (espnow_time_packet_t *)data;

    if (pkt->version != ESPNOW_TIME_VERSION) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Unsupported time packet version: %d", pkt->version);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGD(TAG, "Received time packet from " MACSTR ", type: %d", MAC2STR(src_addr), pkt->type);

    switch (g_time_role) {
        case ESPNOW_TIME_ROLE_INITIATOR:
            /* Initiator: respond to time requests by broadcasting time */
            if (pkt->type == ESPNOW_TIME_TYPE_REQUEST) {
                /* The REQUEST is unauthenticated. While replies fail (for example
                 * CONFIG_ESPNOW_TIME_SECURITY is on and no application key is set
                 * yet) hold the reply path for ESPNOW_TIME_REQ_FAIL_HOLD_MS so a
                 * flood of REQUESTs cannot drive the failing send path, and its
                 * diagnostics in espnow_send(), once per packet. Successful replies
                 * are not limited. */
                uint32_t now_ms = esp_log_timestamp();
                if (g_req_fail_ms && (uint32_t)(now_ms - g_req_fail_ms) < ESPNOW_TIME_REQ_FAIL_HOLD_MS) {
                    break;
                }
                ESP_LOGD(TAG, "Received time request, broadcasting time");
                if (espnow_time_send_packet(ESPNOW_TIME_TYPE_BROADCAST, true) == ESP_OK) {
                    g_req_fail_ms = 0;
                } else {
                    /* The hold starts when the send has failed, not when the REQUEST
                     * arrived: espnow_send() may have blocked for up to its 1000 ms
                     * timeout waiting for the send lock, and a hold measured from the
                     * arrival time would already be over for the next queued REQUEST. */
                    uint32_t fail_ms = esp_log_timestamp();
                    g_req_fail_ms = fail_ms ? fail_ms : 1;
                }
            }
            /* Initiator never adjusts its time */
            break;

        case ESPNOW_TIME_ROLE_RESPONDER:
            /* Responder: adjust time if packet is a broadcast from initiator */
            if (pkt->type == ESPNOW_TIME_TYPE_BROADCAST) {
                int64_t local_time_us = esp_timer_get_time();
                int64_t remote_time_us = pkt->timestamp_us;
                int32_t drift_ms = (int32_t)((remote_time_us - local_time_us - g_time_offset_us) / 1000);

                ESP_LOGD(TAG, "Time drift from initiator: %" PRId32 " ms", drift_ms);

                /* Only adjust if drift exceeds threshold */
                if (abs(drift_ms) > g_responder_config.max_drift_ms) {
                    g_time_offset_us = remote_time_us - local_time_us;
                    ESPNOW_LOG_RATE_LIMITED(ESP_LOGI, 1000, "Time adjusted by %" PRId32 " ms", drift_ms);
                }

                /* Post event to notify application */
                espnow_timesync_event_t event_data = {
                    .drift_ms = drift_ms,
                    .synced_time_us = esp_timer_get_time() + g_time_offset_us,
                };
                memcpy(event_data.src_addr, src_addr, 6);
                esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_TIMESYNC_SYNCED, &event_data, sizeof(event_data), 0);
            }
            break;

        default:
            ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Time sync not started, ignoring packet");
            break;
    }

    return ESP_OK;
}

/**
 * @brief Timer callback for periodic time broadcast (initiator only)
 */
static void espnow_time_timer_cb(TimerHandle_t timer)
{
    if (g_time_role == ESPNOW_TIME_ROLE_INITIATOR) {
        espnow_time_send_packet(ESPNOW_TIME_TYPE_BROADCAST, false);
    }
}

esp_err_t espnow_time_initiator_start(const espnow_time_initiator_config_t *config)
{
    if (g_time_role != ESPNOW_TIME_ROLE_NONE) {
        ESP_LOGW(TAG, "Time sync already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Use default config if NULL */
    if (config) {
        memcpy(&g_initiator_config, config, sizeof(espnow_time_initiator_config_t));
    } else {
        g_initiator_config.sync_interval_ms = 0;
    }

    /* Register handler for time sync data type */
    esp_err_t ret = espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_TIMESYNC, true, espnow_time_recv_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register time sync handler: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create periodic sync timer if interval is set */
    if (g_initiator_config.sync_interval_ms > 0) {
        g_initiator_timer = xTimerCreate("espnow_time",
                                     pdMS_TO_TICKS(g_initiator_config.sync_interval_ms),
                                     pdTRUE,  /* Auto-reload */
                                     NULL,
                                     espnow_time_timer_cb);
        if (g_initiator_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create sync timer");
            espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_TIMESYNC, false, NULL);
            return ESP_ERR_NO_MEM;
        }
        xTimerStart(g_initiator_timer, 0);
    }

    g_time_role = ESPNOW_TIME_ROLE_INITIATOR;
    g_req_fail_ms = 0;

    return ESP_OK;
}

esp_err_t espnow_time_initiator_stop(void)
{
    if (g_time_role != ESPNOW_TIME_ROLE_INITIATOR) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_initiator_timer) {
        xTimerStop(g_initiator_timer, 0);
        xTimerDelete(g_initiator_timer, 0);
        g_initiator_timer = NULL;
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_TIMESYNC, false, NULL);

    g_time_role = ESPNOW_TIME_ROLE_NONE;

    ESP_LOGI(TAG, "Initiator stopped");
    return ESP_OK;
}

esp_err_t espnow_time_initiator_broadcast(void)
{
    if (g_time_role != ESPNOW_TIME_ROLE_INITIATOR) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Broadcasting time");
    return espnow_time_send_packet(ESPNOW_TIME_TYPE_BROADCAST, false);
}

esp_err_t espnow_time_responder_start(const espnow_time_responder_config_t *config)
{
    if (g_time_role != ESPNOW_TIME_ROLE_NONE) {
        ESP_LOGW(TAG, "Time sync already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Use default config if NULL */
    if (config) {
        memcpy(&g_responder_config, config, sizeof(espnow_time_responder_config_t));
    } else {
        g_responder_config.max_drift_ms = 100;
    }

    /* Register handler for time sync data type */
    esp_err_t ret = espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_TIMESYNC, true, espnow_time_recv_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register time sync handler: %s", esp_err_to_name(ret));
        return ret;
    }

    g_time_role = ESPNOW_TIME_ROLE_RESPONDER;

    return ESP_OK;
}

esp_err_t espnow_time_responder_stop(void)
{
    if (g_time_role != ESPNOW_TIME_ROLE_RESPONDER) {
        return ESP_ERR_INVALID_STATE;
    }

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_TIMESYNC, false, NULL);

    g_time_role = ESPNOW_TIME_ROLE_NONE;
    g_time_offset_us = 0;

    ESP_LOGI(TAG, "Responder stopped");
    return ESP_OK;
}

esp_err_t espnow_time_responder_request(void)
{
    if (g_time_role != ESPNOW_TIME_ROLE_RESPONDER) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Send time request to trigger initiator broadcast */
    esp_err_t ret = espnow_time_send_packet(ESPNOW_TIME_TYPE_REQUEST, false);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGD(TAG, "Time request sent");
    return ESP_OK;
}
