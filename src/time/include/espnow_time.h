/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file espnow_time.h
 * @brief ESP-NOW Internal Time Synchronization
 *
 * This module provides time synchronization between ESP-NOW nodes without
 * requiring internet connectivity.
 *
 * Roles:
 * - Initiator (Controller): Broadcasts authoritative time, never adjusts its own time
 * - Responder (Data Node): Receives time broadcasts and adjusts its time accordingly
 *
 * This is similar to OTA where Initiator pushes firmware and Responder receives it.
 *
 * This solves the issue where nodes waking from deep sleep would
 * incorrectly reset the controller's time.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "esp_wifi_types.h"

#include "espnow.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Enumerated list of time sync event id
 */
#define ESP_EVENT_ESPNOW_TIMESYNC_STARTED       (ESP_EVENT_ESPNOW_TIMESYNC_BASE + 0)  /**< Time sync module started */
#define ESP_EVENT_ESPNOW_TIMESYNC_STOPPED       (ESP_EVENT_ESPNOW_TIMESYNC_BASE + 1)  /**< Time sync module stopped */
#define ESP_EVENT_ESPNOW_TIMESYNC_SYNCED        (ESP_EVENT_ESPNOW_TIMESYNC_BASE + 2)  /**< Time synchronized with initiator */
#define ESP_EVENT_ESPNOW_TIMESYNC_TIMEOUT       (ESP_EVENT_ESPNOW_TIMESYNC_BASE + 3)  /**< Time sync request timeout */

/**
 * @brief Time sync event data for ESP_EVENT_ESPNOW_TIMESYNC_SYNCED
 */
typedef struct {
    uint8_t src_addr[6];           /**< MAC address of the initiator */
    int32_t drift_ms;              /**< Time drift in milliseconds */
    int64_t synced_time_us;        /**< Synchronized time in microseconds */
} espnow_timesync_event_t;

/**
 * @brief Time synchronization packet structure
 */
typedef struct {
    uint8_t version;               /**< Protocol version */
    uint8_t type;                  /**< Packet type: broadcast or request */
    int64_t timestamp_us;          /**< Sender's timestamp in microseconds since boot */
    int64_t utc_time_us;           /**< Sender's UTC time in microseconds (0 if not available) */
} __attribute__((packed)) espnow_time_packet_t;

/**
 * @brief Initiator configuration (controller/time broadcaster)
 */
typedef struct {
    uint32_t sync_interval_ms;     /**< Interval to broadcast time (0 = on demand only) */
} espnow_time_initiator_config_t;

/**
 * @brief Responder configuration (data node/time receiver)
 */
typedef struct {
    int32_t max_drift_ms;          /**< Maximum acceptable time drift before adjustment (default: 100ms) */
} espnow_time_responder_config_t;

/**
 * @brief Default initiator configuration
 */
#define ESPNOW_TIME_INITIATOR_CONFIG_DEFAULT() { \
    .sync_interval_ms = 0, \
}

/**
 * @brief Default responder configuration
 */
#define ESPNOW_TIME_RESPONDER_CONFIG_DEFAULT() { \
    .max_drift_ms = 100, \
}

/**
 * @brief Start time synchronization initiator (controller/time broadcaster)
 *
 * @note The initiator broadcasts authoritative time and never adjusts its own time
 *       based on other nodes. It responds to time requests from responders.
 *
 * @param[in] config  Initiator configuration, use ESPNOW_TIME_INITIATOR_CONFIG_DEFAULT() if NULL
 *
 * @return
 *    - ESP_OK on success
 *    - ESP_ERR_INVALID_STATE if already started
 */
esp_err_t espnow_time_initiator_start(const espnow_time_initiator_config_t *config);

/**
 * @brief Stop time synchronization initiator
 *
 * @return
 *    - ESP_OK on success
 *    - ESP_ERR_INVALID_STATE if not started
 */
esp_err_t espnow_time_initiator_stop(void);

/**
 * @brief Broadcast current time to all nodes (for initiator)
 *
 * @note This function is automatically called if sync_interval_ms is set.
 *       Can also be called manually to immediately broadcast time.
 *
 * @return
 *    - ESP_OK on success
 *    - ESP_ERR_INVALID_STATE if initiator not started
 */
esp_err_t espnow_time_initiator_broadcast(void);

/**
 * @brief Start time synchronization responder (data node/time receiver)
 *
 * @note The responder adjusts its time based on the initiator's broadcasts.
 *       Typically used for nodes that wake from deep sleep.
 *
 * @param[in] config  Responder configuration, use ESPNOW_TIME_RESPONDER_CONFIG_DEFAULT() if NULL
 *
 * @return
 *    - ESP_OK on success
 *    - ESP_ERR_INVALID_STATE if already started
 */
esp_err_t espnow_time_responder_start(const espnow_time_responder_config_t *config);

/**
 * @brief Stop time synchronization responder
 *
 * @return
 *    - ESP_OK on success
 *    - ESP_ERR_INVALID_STATE if not started
 */
esp_err_t espnow_time_responder_stop(void);

/**
 * @brief Request time synchronization from initiator
 *
 * @note Call this after waking from deep sleep to synchronize time.
 *       The responder will automatically adjust its time upon receiving
 *       a broadcast from the initiator. Use ESP_EVENT_ESPNOW_TIMESYNC_SYNCED
 *       event to get notified when synchronization completes.
 *
 * @return
 *    - ESP_OK on success (request sent)
 *    - ESP_ERR_INVALID_STATE if responder not started
 */
esp_err_t espnow_time_responder_request(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
