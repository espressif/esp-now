/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_now.h"
#include "esp_event.h"

#include "espnow.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Enumerated list of control event id
 */
#define ESP_EVENT_ESPNOW_CTRL_BIND          (ESP_EVENT_ESPNOW_CTRL_BASE + 0)
#define ESP_EVENT_ESPNOW_CTRL_UNBIND        (ESP_EVENT_ESPNOW_CTRL_BASE + 1)
#define ESP_EVENT_ESPNOW_CTRL_BIND_ERROR    (ESP_EVENT_ESPNOW_CTRL_BASE + 2)

/**
 * @brief Maximum number of device in bind list
 */
#define ESPNOW_BIND_LIST_MAX_SIZE  32

/**
 * @brief Control attribute.
 * The high byte is device type, the low byte is device attribute.
 */
typedef enum {
    ESPNOW_ATTRIBUTE_BASE              = 0x0000,
    ESPNOW_ATTRIBUTE_POWER              = 0x0001,
    ESPNOW_ATTRIBUTE_POWER_ADD          = 0x0002,

    ESPNOW_ATTRIBUTE_ATTRIBUTE          = 0x0003,

    /**< light */
    ESPNOW_ATTRIBUTE_LIGHT_BASE         = 0x0100,
    ESPNOW_ATTRIBUTE_BRIGHTNESS         = 0x0101,
    ESPNOW_ATTRIBUTE_BRIGHTNESS_ADD     = 0x0102,
    ESPNOW_ATTRIBUTE_HUE                = 0x0103,
    ESPNOW_ATTRIBUTE_HUE_ADD            = 0x0104,
    ESPNOW_ATTRIBUTE_SATURATION         = 0x0105,
    ESPNOW_ATTRIBUTE_SATURATION_ADD     = 0x0106,
    ESPNOW_ATTRIBUTE_WARM               = 0x0107,
    ESPNOW_ATTRIBUTE_WARM_ADD           = 0x0108,
    ESPNOW_ATTRIBUTE_COLD               = 0x0109,
    ESPNOW_ATTRIBUTE_COLD_ADD           = 0x010a,
    ESPNOW_ATTRIBUTE_RED                = 0x010b,
    ESPNOW_ATTRIBUTE_RED_ADD            = 0x010c,
    ESPNOW_ATTRIBUTE_GREEN              = 0x010d,
    ESPNOW_ATTRIBUTE_GREEN_ADD          = 0x010e,
    ESPNOW_ATTRIBUTE_BLUE               = 0x010f,
    ESPNOW_ATTRIBUTE_BLUE_ADD           = 0x0110,
    ESPNOW_ATTRIBUTE_MODE               = 0x0111,
    ESPNOW_ATTRIBUTE_MODE_ADD           = 0x0112,

    /**< button */
    ESPNOW_ATTRIBUTE_BUTTON_BASE        = 0x0200,
    ESPNOW_ATTRIBUTE_KEY_1              = 0x0201,
    ESPNOW_ATTRIBUTE_KEY_2              = 0x0202,
    ESPNOW_ATTRIBUTE_KEY_3              = 0x0203,
    ESPNOW_ATTRIBUTE_KEY_4              = 0x0204,
    ESPNOW_ATTRIBUTE_KEY_5              = 0x0205,
    ESPNOW_ATTRIBUTE_KEY_6              = 0x0206,
    ESPNOW_ATTRIBUTE_KEY_7              = 0x0207,
    ESPNOW_ATTRIBUTE_KEY_8              = 0x0208,
    ESPNOW_ATTRIBUTE_KEY_9              = 0x0209,
    ESPNOW_ATTRIBUTE_KEY_10             = 0x020a,

    /**< battery */
    ESPNOW_ATTRIBUTE_BATTERY_BASE       = 0x0300,
    ESPNOW_ATTRIBUTE_STATUS_LOW_BATTERY = 0x0301,
    ESPNOW_ATTRIBUTE_BATTERY_LEVEL      = 0x0302,
    ESPNOW_ATTRIBUTE_CHARGING_STATE     = 0x0303,
} espnow_attribute_t;

typedef enum {
    ESPNOW_BIND_ERROR_NONE,
    ESPNOW_BIND_ERROR_TIMEOUT,
    ESPNOW_BIND_ERROR_RSSI,
    ESPNOW_BIND_ERROR_LIST_FULL,
} espnow_ctrl_bind_error_t;

/**
 * @brief Bind information from initiator
 */
typedef struct espnow_ctrl_bind_info_s {
    uint8_t mac[6];                         /**< Initiator's MAC address */
    espnow_attribute_t initiator_attribute; /**< Initiator's attribute */
} espnow_ctrl_bind_info_t;

/**
 * @brief Control data from initiator
 */
typedef struct espnow_ctrl_data_s {
#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
    espnow_frame_head_t frame_head;
#endif
    espnow_attribute_t initiator_attribute;         /**< Initiator's attribute */
    espnow_attribute_t responder_attribute;         /**< Responder's attribute */
    union {
        bool responder_value_b;   /**< Boolean */
        int responder_value_i;    /**< Integer */
        float responder_value_f;  /**< Float */
        struct {
            uint32_t responder_value_s_flag : 24; /**< String flag, the value is 0x00, 0x00, 0x00 */
            uint8_t responder_value_s_size;       /**< String length */
        };
    };

    char responder_value_s[0];   /**< String value, present when responder_value_s_flag == 0 and
                                      responder_value_s_size != 0. In the raw callback it is always
                                      responder_value_s_size bytes followed by a NUL. An integer or
                                      float with the low 24 bits 0 (0x02000000, 2.0f) has the same
                                      header bytes; when the frame does not hold the whole string
                                      the value is the one in responder_value_i and these bytes
                                      are all 0 (an empty string) */
} espnow_ctrl_data_t;

/**
 * @brief  The bind callback function
 *
 * @attention Each time a bind frame is received, the callback function will be called.
 *
 * @param[in]  initiator_attribute  initiator attribute
 * @param[in]  mac  initiator mac address
 * @param[in]  rssi  rssi of bind information
 *
 * @return
 *    - TRUE
 *    - FALSE
 */
typedef bool (* espnow_ctrl_bind_cb_t)(espnow_attribute_t initiator_attribute, uint8_t mac[6], int8_t rssi);

/**
 * @brief  The control data callback function
 *
 * @param[in]  initiator_attribute  the received initiator's attribute
 * @param[in]  responder_attribute  the received responder's attribute
 * @param[in]  responder_value  the received responder value
 *
 */
typedef void (* espnow_ctrl_data_cb_t)(espnow_attribute_t initiator_attribute,
                                       espnow_attribute_t responder_attribute,
                                       uint32_t responder_value);

/**
 * @brief  The raw control data callback function
 *
 * @note `data` is only valid during the callback and comes without a length. What may
 *       be read is fixed by its header:
 *       - responder_value_s_flag != 0 or responder_value_s_size == 0: the header only;
 *         there is no responder_value_s.
 *       - otherwise: the header, responder_value_s[0 .. responder_value_s_size - 1] and
 *         a NUL at responder_value_s[responder_value_s_size]. This holds for every
 *         frame, whatever its length on the air.
 *       The union has no type tag: an integer or float whose low 24 bits are 0
 *       (0x02000000, 2.0f) has the bytes of a string header and is sent without a
 *       string. When the frame does not hold the whole announced string,
 *       responder_value_s is all 0 (an empty string) and the value is the one in
 *       responder_value_i / responder_value_f.
 *       The callback runs on the espnow task. A frame that is shorter than its header
 *       announces, or that carries the string without the NUL, is copied to a buffer
 *       on that task's stack first (up to sizeof(espnow_ctrl_data_t) + 256 bytes, no
 *       heap allocation); size CONFIG_ESPNOW_TASK_STACK_SIZE for the callback plus that.
 *
 * @param[in]  src_addr  MAC address of sender
 * @param[in]  data  control data from sender
 * @param[in]  rx_ctrl  received packet radio metadata header
 *
 */
typedef void (* espnow_ctrl_data_raw_cb_t)(espnow_addr_t src_addr, espnow_ctrl_data_t *data, wifi_pkt_rx_ctrl_t *rx_ctrl);

/**
 * @brief  The initiator sends a broadcast bind frame
 *
 * @param[in]  initiator_attribute  initiator attribute
 * @param[in]  enable  bind or unbind
 *
 * @return
 *    - TRUE
 *    - FALSE
 */
esp_err_t espnow_ctrl_initiator_bind(espnow_attribute_t initiator_attribute, bool enable);

/**
 * @brief  The initiator sends a broadcast control data frame
 *
 * @note Design limitation: control frames carry no sequence number and can be
 *       replayed even when encrypted (see the espnow_set_key note). espnow_ctrl
 *       is designed for reversible convenience control (lights, switches, levels)
 *       where a replayed command repeats a visible, reversible state the user
 *       can undo. Recommended usage: absolute attributes (the *_ADD attributes
 *       accumulate on replay), an idempotent responder callback that also rate
 *       limits state changes (for example one per second; per responder, since
 *       src_addr is not authenticated, see espnow_ctrl_responder_data), and the
 *       bind window closed after binding (see espnow_ctrl_responder_bind). Within
 *       this usage a replay has no persistent effect: it repeats a reversible
 *       state only while the attacker keeps transmitting, writes nothing to
 *       flash, discloses nothing and cannot unbind the initiator. Driving
 *       unlock, arm/disarm, OTA triggers, credential changes or any action that
 *       must not repeat from espnow_ctrl is not recommended and not protected;
 *       carry such commands in an application DATA frame with a counter.
 *
 * @param[in]  initiator_attribute  the sending initiator's attribute
 * @param[in]  responder_attribute  the sending responder's attribute
 * @param[in]  responder_value  the sending responder value
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_initiator_send(espnow_attribute_t initiator_attribute, espnow_attribute_t responder_attribute, uint32_t responder_value);

/**
 * @brief  The responder creates a bind task to process the received bind frame
 *
 * @attention  The wait time, RSSI threshold and callback gate binding only. An unbind
 *             frame matching an entry in the bind list is accepted at any time and
 *             written to flash immediately.
 *
 * @attention  The bind list is matched on the outer source address, which is
 *             never authenticated (see the espnow_set_key note). Without
 *             CONFIG_ESPNOW_CONTROL_SECURITY anyone can create a control frame in
 *             a bound initiator's name; with it, a device without the key can
 *             still replay a captured frame with that address.
 *
 * @attention  Recommended usage: binding is a user-triggered step. The CONTROL_BIND
 *             handler stays registered after wait_ms, so a replayed unbind frame is
 *             accepted at any time; this is the only persistent effect a replayed
 *             control frame can have, and it is removed by closing the window:
 *             after the bind window call
 *             espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_CONTROL_BIND, false, NULL)
 *             and manage the list with espnow_ctrl_responder_*_bindlist. Reopen
 *             only on a user action with a short wait_ms and an RSSI threshold;
 *             leaving the bind handler registered permanently is not recommended.
 *
 * @param[in]  wait_ms  maximum waiting bind time in millisecond
 * @param[in]  rssi  the minimum bind frame RSSI
 * @param[in]  cb  the bind callback function
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_responder_bind(uint32_t wait_ms, int8_t rssi, espnow_ctrl_bind_cb_t cb);

/**
 * @brief  The responder registers control data callback function
 *
 * @note The handler stays registered and the callback has no freshness
 *       information, so any frame a bound initiator ever sent can be replayed
 *       to it without the key (see espnow_ctrl_initiator_send). Use it only for
 *       reversible, idempotent, rate-limited state.
 *
 * @note src_addr comes from the outer ESP-NOW header, which is not authenticated
 *       (encryption covers the payload only, without associated data), and the
 *       bind list is matched on that address. A recorded ciphertext can therefore
 *       be re-sent under the address of any other bound initiator with the same
 *       attribute, so state kept per src_addr (counters, rate limits) is not a
 *       security measure: a counter kept only per src_addr is bypassed by
 *       replaying one initiator's frame under another's address. Where a
 *       repeated command matters, both the sender identity and a monotonic
 *       counter must be inside the encrypted payload (for example a custom
 *       attribute whose responder_value_s carries "<initiator MAC>:<counter>"),
 *       the receiver must keep the counter per that authenticated identity and
 *       accept a frame only if the identity equals src_addr (so the bind check
 *       applied to it) and the counter is greater than the last one accepted
 *       for it. This holds within the shared-key trust domain only: every key
 *       holder can create such a payload for any identity. Without this,
 *       restrict espnow_ctrl to actions whose replay is harmless.
 *
 * @param[in]  cb  the control data callback function
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_responder_data(espnow_ctrl_data_cb_t cb);

/**
 * @brief  The responder gets bound list
 *
 * @param[out]  list  the buffer that stores the bound list
 * @param[inout]  size  input maximum bound list size, output the real bound list size
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_responder_get_bindlist(espnow_ctrl_bind_info_t *list, size_t *size);

/**
 * @brief  The responder sets bound list
 *
 * @attention  The bound information will be stored to flash
 *
 * @param[in]  info  the bound information to be set
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_responder_set_bindlist(const espnow_ctrl_bind_info_t *info);

/**
 * @brief  The responder removes bound list
 *
 * @attention  The bound information will be removed from flash
 *
 * @param[in]  info  the bound information to be removed
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_responder_remove_bindlist(const espnow_ctrl_bind_info_t *info);

/**
 * @brief Remove all bound devices from bindlist
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_responder_clear_bindlist(void);

/**
 * @brief  Send control data frame
 *
 * @param[in]  dest_addr  dest_addr is not used
 * @param[in]  data  control data
 * @param[in]  frame_head  frame header must not be NULL
 * @param[in]  wait_ticks  the maximum waiting time in ticks
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_send(const espnow_addr_t dest_addr, const espnow_ctrl_data_t *data, const espnow_frame_head_t *frame_head, TickType_t wait_ticks);

/**
 * @brief  The responder registers raw control data callback function
 *
 * @param[in]  cb  the raw control data callback function
 *
 * @return
 *    - ESP_OK: succeed
 *    - others: fail
 */
esp_err_t espnow_ctrl_recv(espnow_ctrl_data_raw_cb_t cb);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
