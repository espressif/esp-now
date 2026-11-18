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

#pragma once

#include "esp_now.h"

#include "espnow_utils.h"
#include "espnow_security.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#define ESPNOW_PACKED_STRUCT                __attribute__ ((packed))

/* Maximum user payload per frame: follows ESP_NOW_MAX_DATA_LEN_V2 on
 * ESP-NOW v2 capable SDKs (IDF >= v5.4), otherwise stays at the historical
 * 230 bytes. The on-wire header layout is unchanged; see espnow_data_t in
 * espnow.c. */
#if defined(ESP_NOW_MAX_DATA_LEN_V2)
#define ESPNOW_PAYLOAD_LEN                  (ESP_NOW_MAX_DATA_LEN_V2 - 20) /* minus sizeof(espnow_data_t) */
#else
#define ESPNOW_PAYLOAD_LEN                  (230)
#endif

#ifdef CONFIG_ESPNOW_APP_SECURITY
#define ESPNOW_DATA_LEN                     ESPNOW_SEC_PACKET_MAX_SIZE
#else
#define ESPNOW_DATA_LEN                     ESPNOW_PAYLOAD_LEN
#endif

#define ESPNOW_ADDR_LEN                     (6)
#define ESPNOW_ADDR_IS_EMPTY(addr)          (((addr)[0] | (addr)[1] | (addr)[2] | (addr)[3] | (addr)[4] | (addr)[5]) == 0x0)
#define ESPNOW_ADDR_IS_BROADCAST(addr)      (((addr)[0] & (addr)[1] & (addr)[2] & (addr)[3] & (addr)[4] & (addr)[5]) == 0xFF)
#define ESPNOW_ADDR_IS_SELF(addr)           !memcmp(addr, ESPNOW_ADDR_SELF, 6)
#define ESPNOW_ADDR_IS_EQUAL(addr1, addr2)  !memcmp(addr1, addr2, 6)

typedef uint8_t espnow_addr_t[6];
typedef uint8_t espnow_group_t[6];

extern const uint8_t ESPNOW_ADDR_NONE[6];
extern const uint8_t ESPNOW_ADDR_BROADCAST[6];
extern const uint8_t ESPNOW_ADDR_GROUP_OTA[6];
extern const uint8_t ESPNOW_ADDR_GROUP_SEC[6];
extern const uint8_t ESPNOW_ADDR_GROUP_PROV[6];

/**
 * @brief Declaration of the task events family
 */
ESP_EVENT_DECLARE_BASE(ESP_EVENT_ESPNOW);

#define ESP_EVENT_ESPNOW_PROV_BASE          0x100
#define ESP_EVENT_ESPNOW_CTRL_BASE          0x200
#define ESP_EVENT_ESPNOW_OTA_BASE           0x300
#define ESP_EVENT_ESPNOW_DEBUG_BASE         0x400
#define ESP_EVENT_ESPNOW_TIMESYNC_BASE      0x500
#define ESP_EVENT_ESPNOW_RESERVED_BASE      0x600

/**
 * @brief The channel on which the device sends packets
 */
#define ESPNOW_CHANNEL_CURRENT               0x0   /**< Only in the current channel */
#define ESPNOW_CHANNEL_ALL                   0x0f  /**< All supported channels */

#define ESPNOW_RETRANSMIT_MAX_COUNT          0x1f  /**< Maximum number of retransmissions */
#define ESPNOW_FORWARD_MAX_COUNT             0x1f  /**< Maximum number of forwards */

/**
 * @brief Initialize the configuration of espnow
 *
 * @note ESP-NOW does not authenticate the sender address. The library only
 *       verifies possession of the application key (AES128-CCM on the payload),
 *       so a plaintext data type accepts frames from any device in range and all
 *       key holders form one trust domain. There is no anti-replay; see the
 *       espnow_set_key note.
 *
 *       Recommended security configuration; without all of it the library provides no
 *       authentication:
 *       - sec_enable set and the key handshake completed before protected traffic;
 *       - CONFIG_ESPNOW_ALL_SECURITY, or CONFIG_ESPNOW_*_SECURITY for every protected type;
 *       - CONFIG_ESPNOW_SEC_REJECT_PLAINTEXT and CONFIG_ESPNOW_SEC_IGNORE_REST;
 *       - forward_enable cleared unless unauthenticated multi-hop relay is needed;
 *       - a non-public, random PoP with enough entropy to be unpredictable, per
 *         deployment or per device, provisioned to both sides over a trusted
 *         channel, never the examples' public espnow_pop: the PoP is the only
 *         secret protecting the application key. Use at least
 *         ESPNOW_SEC_POP_RECOMMENDED_LEN random characters;
 *       - the handshake run as a commissioning step and every node verified afterwards
 *         (see the espnow_sec_initiator_start note);
 *       - espnow_sec_initiator_start called only with the addresses of responders
 *         that the application has approved. An address can be spoofed, so this
 *         narrows the handshake but does not replace the PoP;
 *       - Flash Encryption (the application key is stored in NVS in plaintext),
 *         plus Secure Boot v2 and CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK when
 *         espnow_ota is used;
 *       - the ESP-NOW debug console disabled (espnow_console_config_t
 *         monitor_command.espnow cleared);
 *       - log level at most INFO (DEBUG prints the key).
 *
 *       Design limitations and recommended usage. ESP-NOW frames have no sequence
 *       number and the header is not authenticated, so a recorded frame can be
 *       replayed and its type or source address rewritten (espnow_set_key note);
 *       GROUP and ACK are plaintext by design (espnow_set_group and
 *       espnow_frame_head_t notes). The recommended usage within these limits:
 *       - application data carries its own type tag and counter in the payload;
 *       - built-in protocols are used only for what they are designed for:
 *         espnow_ctrl for reversible convenience control, espnow_time for
 *         non-security time, groups configured locally, ACK as a transport hint;
 *       - every receive window (handshake, bind, OTA, provisioning request and
 *         reply, time sync) is open only for the operation it serves, on an
 *         explicit action, and closed afterwards; a recorded encrypted frame
 *         can be replayed without the key, so an open window is the exposure.
 *       Using the library outside this usage (for example unlock or alarm
 *       control over espnow_ctrl, a handshake responder left running after
 *       commissioning, over-the-air grouping outside commissioning) is not
 *       recommended and is not protected.
 *
 *       The recommended security configuration is both parts of this note, the
 *       build options and the usage. The examples, including examples/solution,
 *       are lab demos that implement neither (plaintext defaults, public PoP,
 *       handlers registered for the whole uptime, no token or counter checks);
 *       building one with the options above does not make it the recommended
 *       configuration. Behaviour reproduced on an example is a property of the
 *       example, not of the library under the recommended configuration.
 */
typedef struct {
    const uint8_t pmk[16];                  /**< Primary master key */
    bool forward_enable             : 1;    /**< Forward when packets are received. A broadcast frame is relayed before it
                                                 is decrypted or authenticated, bounded by forward_ttl and retransmit_count,
                                                 so any device in range can make the network repeat a frame. Clear it when
                                                 multi-hop relay is not needed. */
    bool forward_switch_channel     : 1;    /**< Forward data packet with exchange channel */
    bool sec_enable                 : 1;    /**< Enable ESP-NOW data payload encryption when send and decryption when receive.
                                                 It encrypts nothing by itself: a frame is encrypted only when it is sent with
                                                 a frame head that has security set (see the espnow_send note).
                                                 With CONFIG_ESPNOW_SEC_REJECT_PLAINTEXT (recommended) plaintext frames of
                                                 types built with CONFIG_ESPNOW_*_SECURITY, DATA and RESERVED are rejected;
                                                 see espnow_set_security_required_for_data_type(). */
    uint8_t reserved1               : 5;    /**< Reserved */
    uint8_t qsize;                          /**< Size of packet buffer queue. A received frame takes one entry to be handled
                                                 by this device and one more to be forwarded. A frame that needs both while
                                                 only one is free is handled and not forwarded, so use at least 2 together
                                                 with forward_enable. */
    uint8_t send_retry_num;                 /**< Number of retransmissions */
    uint32_t send_max_timeout;              /**< Maximum timeout */
    struct {
        bool ack                    : 1;    /**< Enable or disable ACK */
        bool forward                : 1;    /**< Enable or disable forward */
        bool group                  : 1;    /**< Enable or disable group */
        bool provisioning            : 1;    /**< Enable or disable provisioning */
        bool control_bind           : 1;    /**< Enable or disable control bind */
        bool control_data           : 1;    /**< Enable or disable control data */
        bool ota_status             : 1;    /**< Enable or disable OTA status */
        bool ota_data               : 1;    /**< Enable or disable OTA data */
        bool debug_log              : 1;    /**< Enable or disable debug LOG */
        bool debug_command          : 1;    /**< Enable or disable debug command */
        bool data                   : 1;    /**< Enable or disable data */
        bool sec_status             : 1;    /**< Enable or disable security status */
        bool sec                    : 1;    /**< Enable or disable security */
        bool sec_data               : 1;    /**< Enable or disable security data */
        bool timesync               : 1;    /**< Enable or disable time synchronization */
        uint32_t reserved2          : 17;   /**< Reserved */
    } receive_enable;            /**< Set 1 to enable receiving the corresponding ESP-NOW data type */
} espnow_config_t;

#define ESPNOW_INIT_CONFIG_DEFAULT() { \
    .pmk = "ESP_NOW", \
    .forward_enable = 1, \
    .forward_switch_channel = 0, \
    .sec_enable = 0, \
    .reserved1 = 0,   \
    .qsize = 32, \
    .send_retry_num = 10, \
    .send_max_timeout = pdMS_TO_TICKS(3000),\
    .receive_enable = { \
                .ack           = 1, \
                .forward       = 1, \
                .group         = 1, \
                .provisioning   = 0, \
                .control_bind  = 0, \
                .control_data  = 0, \
                .ota_status    = 0, \
                .ota_data      = 0, \
                .debug_log     = 0, \
                .debug_command = 0, \
                .data          = 0, \
                .sec_status    = 0, \
                .sec           = 0, \
                .sec_data      = 0, \
                .timesync      = 0, \
                .reserved2     = 0, \
                }, \
    }

/**
 * @brief Divide ESP-NOW data into multiple pipes
 */
typedef enum {
    ESPNOW_DATA_TYPE_ACK,            /**< For reliable data transmission */
    ESPNOW_DATA_TYPE_FORWARD,        /**< Set to forward packets */
    ESPNOW_DATA_TYPE_GROUP,          /**< Send a packet that sets the group type */
    ESPNOW_DATA_TYPE_PROV,           /**< Network configuration packet */
    ESPNOW_DATA_TYPE_CONTROL_BIND,   /**< Binding or unbinding packet */
    ESPNOW_DATA_TYPE_CONTROL_DATA,   /**< Control data packet */
    ESPNOW_DATA_TYPE_OTA_STATUS,     /**< Status packet for rapid upgrade of batch Device */
    ESPNOW_DATA_TYPE_OTA_DATA,       /**< Data packet for rapid upgrade of batch Device */
    ESPNOW_DATA_TYPE_DEBUG_LOG,      /**< Equipment debugging log packet */
    ESPNOW_DATA_TYPE_DEBUG_COMMAND,  /**< Equipment debugging command packet */
    ESPNOW_DATA_TYPE_DATA,           /**< User-defined use */
    ESPNOW_DATA_TYPE_SECURITY_STATUS,/**< Security status packet */
    ESPNOW_DATA_TYPE_SECURITY,       /**< Security handshake packet */
    ESPNOW_DATA_TYPE_SECURITY_DATA,  /**< Security packet */
    ESPNOW_DATA_TYPE_TIMESYNC,       /**< Time synchronization packet */
    ESPNOW_DATA_TYPE_RESERVED,       /**< Reserved for other function */
    ESPNOW_DATA_TYPE_MAX,
} espnow_data_type_t;

/**
 * @brief Frame header of espnow
 */
typedef struct espnow_frame_head_s {
    uint16_t magic;                    /**< Unique identifier of each packet. Packets with the same identifier will be filtered. 0: a random number */
    uint8_t channel              : 4;  /**< Set the channel where the packet is sent, ESPNOW_CHANNEL_CURRENT or ESPNOW_CHANNEL_ALL */
    bool filter_adjacent_channel : 1;  /**< Because ESP-NOW is sent through HT20, it can receive packets from adjacent channels */
    bool filter_weak_signal      : 1;  /**< When the signal received by the receiving device is lower than forward_rssi, frame_head data will be discarded */
    bool security                : 1;  /**< The payload data is encrypted if security is true */
    uint16_t                     : 4;  /**< Reserved */

    /**
     * @brief Configure broadcast
     */
    bool broadcast              : 1;  /**< Packet sent in broadcast mode or unicast mode */
    bool group                  : 1;  /**< Only the group set as broadcast transmission mode is valid */
    bool ack                    : 1;  /**< Wait for the receiving device to return ack to ensure transmission reliability.
                                           ACK frames are plaintext and matched on magic only: they signal delivery, not
                                           that the peer holds the key. By design a forged ACK (the magic is readable)
                                           can stop retransmission; ACK is a transport hint only. Using it as an
                                           application-level confirmation is not recommended: require a secured
                                           application reply when delivery matters. */
    uint16_t retransmit_count   : 5;  /**< Too many packet retransmissions will lead to network congestion */
    uint8_t forward_ttl         : 5;  /**< Remaining hops; decremented on every forward, dropped at 0 */
    int8_t forward_rssi         : 8;  /**< When the data packet signal received by the receiving device is lower than forward_rssi, it will not be transferred,
                                           in order to avoid network congestion caused by packet transfer */
} ESPNOW_PACKED_STRUCT espnow_frame_head_t;

#define ESPNOW_FRAME_CONFIG_DEFAULT() \
    { \
        .broadcast = true, \
        .retransmit_count = 10, \
    }

/**
 * @brief When used for unicast, add the target device
 *
 * @param[in]  addr  peer MAC address
 * @param[in]  lmk  peer local master key that is used to encrypt data.
 *              It can be null or ESP_NOW_KEY_LEN length data
 */
esp_err_t espnow_add_peer(const espnow_addr_t addr, const uint8_t *lmk);

/**
 * @brief When used for unicast, delete the target device
 *
 * @param[in]     addr MAC address
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_del_peer(const espnow_addr_t addr);

/**
 * @brief   Send ESP-NOW data
 *
 * @note With sec_enable set, the payload is encrypted only if frame_config->security
 *       is true. A frame head without the bit is sent in plaintext without error,
 *       and so is a NULL frame_config: ESPNOW_FRAME_CONFIG_DEFAULT leaves the bit
 *       clear whatever CONFIG_ESPNOW_*_SECURITY is set to. Always pass a frame head
 *       with security set for data that must be encrypted.
 *
 * @param[in]   type  ESP-NOW data type defined by espnow_data_type_t
 * @param[in]   dest_addr  destination mac address
 * @param[in]   data  the sending data which must not be NULL
 * @param[in]   size  the maximum length of data, must be no more than ESPNOW_DATA_LEN
 * @param[in]   frame_config  if frame_config is NULL, Use ESPNOW_FRAME_CONFIG_DEFAULT configuration
 * @param[in]   wait_ticks  the maximum sending time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 *    - ESP_ERR_TIMEOUT
 *    - ESP_ERR_WIFI_TIMEOUT
 */
esp_err_t espnow_send(espnow_data_type_t type, const espnow_addr_t dest_addr, const void *data,
                      size_t size, const espnow_frame_head_t *frame_config, TickType_t wait_ticks);

/**
 * @brief   ESP-NOW data receive callback function for the corresponding data type
 *
 * @note src_addr and the data type come from the unauthenticated header, even for
 *       an encrypted frame (see the espnow_set_key note): never authorize on
 *       src_addr alone, start every encrypted payload with its own type tag, and
 *       check tag and length before using any field.
 *
 * @param[in]  src_addr  peer MAC address
 * @param[in]  data  received data
 * @param[in]  size  length of received data
 * @param[in]  rx_ctrl  received packet radio metadata header
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
typedef esp_err_t (*handler_for_data_t)(uint8_t *src_addr, void *data,
                                   size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl);

/**
 * @brief De-initialize ESP-NOW function
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_ESPNOW_NOT_INIT
 */
esp_err_t espnow_deinit(void);

/**
 * @brief Initialize ESP-NOW function
 *
 * @param[in]  config  configuration of ESP-NOW
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_init(const espnow_config_t *config);

/**
 * @brief Set configuration when receiving the corresponding ESP-NOW data type
 *        Include: Set to enable/disable handling the corresponding ESP-NOW data type
 *                 Set the callback function when receiving the corresponding ESP-NOW data type
 *
 * @param[in]  type  data type defined by espnow_data_type_t
 * @param[in]  enable  enable or disable the receive of data type, false - disable, true - enable
 * @param[in]  handle  the receive callback function for data type
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_set_config_for_data_type(espnow_data_type_t type, bool enable, handler_for_data_t handle);

/**
 * @brief Get the configuration that whether to handle the corresponding ESP-NOW data type
 *
 * @param[in]  type  data type defined by espnow_data_type_t
 * @param[out]  enable  store the current receiving status of this data type
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_get_config_for_data_type(espnow_data_type_t type, bool *enable);

/**
 * @brief Set whether frames of a data type must arrive encrypted when sec_enable is on
 *
 * With sec_enable set, a plaintext frame of a required type is dropped before the
 * receive callback, which cannot tell a forged plaintext frame from an encrypted one.
 * Built-in types default to their CONFIG_ESPNOW_*_SECURITY option; DATA and RESERVED
 * default to required. ACK, FORWARD, GROUP and SECURITY* are plaintext by design.
 *
 * @note The drop only happens when CONFIG_ESPNOW_SEC_REJECT_PLAINTEXT is enabled
 *       (default n, recommended y). Without it this setting is recorded but has
 *       no effect and plaintext frames of every type are accepted.
 *
 * @note May be called from any task. Calls for different data types do not affect
 *       each other's setting; for one data type the last call wins.
 *
 * @param[in]  type  data type defined by espnow_data_type_t
 * @param[in]  required  true - drop plaintext frames of this type, false - accept them
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_set_security_required_for_data_type(espnow_data_type_t type, bool required);

/**
 * @brief Get whether frames of a data type must arrive encrypted when sec_enable is on
 *
 * @param[in]  type  data type defined by espnow_data_type_t
 * @param[out]  required  current setting for this data type
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_get_security_required_for_data_type(espnow_data_type_t type, bool *required);

/**
 * @brief      Set group ID addresses
 *
 * @param[in]  group_id  pointer to new group ID addresses
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_add_group(const espnow_group_t group_id);

/**
 * @brief      Delete group ID addresses
 *
 * @param[in]  group_id  pointer to deleted group ID address
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_del_group(const espnow_group_t group_id);

/**
 * @brief      Get the number of group ID addresses
 *
 * @return     the number of group ID addresses
 */
int espnow_get_group_num(void);

/**
 * @brief      Get group ID addresses
 *
 * @param[out] group_id_list  pointer to group ID addresses
 * @param[in]  num  the number of group ID addresses
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_get_group_list(espnow_group_t *group_id_list, size_t num);

/**
 * @brief      Check whether the specified group address is my group
 *
 * @param[in]  group_id  pointer to the specified group ID addresses
 *
 * @return     true/false
 */
bool espnow_is_my_group(const espnow_group_t group_id);

/**
 * @brief       Dynamically set the grouping of devices through commands
 *
 * @note GROUP frames are plaintext and unauthenticated, because devices must be
 *       grouped before any key is distributed. Any device in range can change
 *       group membership, and can evict an application group once the list is full;
 *       this exposes no data and grants no control, but it interrupts the current
 *       and later group traffic until membership is restored. Do not use membership
 *       for access control. Recommended usage: configure groups locally with
 *       espnow_add_group() and clear receive_enable.group so no GROUP frame is
 *       processed. Over-the-air grouping is a commissioning convenience; keeping
 *       it enabled afterwards is not recommended, and an application that does
 *       must resend membership before each group session and fall back to unicast.
 *
 * @param[in]   addrs_list  MAC address list of the grouping devices
 * @param[in]   addrs_num  number of the grouping devices
 * @param[in]   group_id  pointer to the specified group ID addresses
 * @param[in]   frame_head  use ESPNOW_FRAME_CONFIG_DEFAULT configuration if frame_config is NULL
 * @param[in]   enable  true: add group, false: delete group
 * @param[in]   wait_ticks  the maximum sending time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_set_group(const espnow_addr_t *addrs_list, size_t addrs_num,
                            const espnow_group_t group_id, espnow_frame_head_t *frame_head,
                            bool enable, TickType_t wait_ticks);

/**
 * @brief Set the security key info
 *        The security key info is used to derive key and stored to flash.
 *        The derived key is used to encrypt ESP-NOW data payload when send and decrypt ESP-NOW data payload when receive.
 *
 * @attention Set sec_enable in espnow_config to true when ESP-NOW initializes, or the function will return failed.
 *
 * @note Encryption covers the payload only and there is no anti-replay (random
 *       IV). The outer type, magic, source and destination address are not
 *       authenticated: a device without the key can record an encrypted frame,
 *       rewrite them (including the source address) and replay it. The key proves
 *       who created a payload, not who sent this frame. Put sender identity and a
 *       sequence number inside the payload where either matters, and prefer
 *       absolute values over relative ones.
 *
 *       These are design limitations of the frame format: the 32-entry duplicate
 *       cache is filled before authentication and is not anti-replay, and the
 *       AEAD has no associated data, so a ciphertext of one type is accepted
 *       under another. Recommended usage: application data carries a type tag
 *       and a monotonic counter or receiver challenge inside the encrypted
 *       payload and rejects stale values (see handler_for_data_t); no
 *       irreversible action is triggered from a single frame; the key is rotated
 *       (new handshake) to invalidate recordings. The built-in protocols are
 *       designed for uses where a replay is harmless or the receive window is
 *       closed; see the recommended usage in the espnow_config_t note.
 *
 * @param[in]  key_info  security key info
 *
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_set_key(uint8_t key_info[APP_KEY_LEN]);

/**
 * @brief Get the security key info stored in flash
 *        If no security key info is stored in flash, the function will return failed.
 *
 * @note The key is stored in NVS in plaintext and is shared by the whole trust
 *       domain: enable Flash Encryption, and re-key when a device is lost.
 *
 * @param[out]  key_info  security key info
 *
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_get_key(uint8_t key_info[APP_KEY_LEN]);

/**
 * @brief Erase the security key info stored in flash
 *
 *    - ESP_OK
 *    - ESP_ERR_NVS_NOT_FOUND
 */
esp_err_t espnow_erase_key(void);

/**
 * @brief Set the security key info
 *        The security key info is used to derive key and stored to flash.
 *        The derived key is used to encrypt ESP-NOW data payload when send and decrypt ESP-NOW data payload when receive.
 *
 * @attention Set sec_enable in espnow_config to true when ESP-NOW initializes, or the function will return failed.
 *
 * @param[in]  key_info  security key info
 *
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_set_dec_key(uint8_t key_info[APP_KEY_LEN]);

/**
 * @brief Get the security key info stored in flash
 *        If no security key info is stored in flash, the function will return failed.
 *
 * @param[out]  key_info  security key info
 *
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_get_dec_key(uint8_t key_info[APP_KEY_LEN]);

/**
 * @brief Erase the security key info stored in flash
 *
 *    - ESP_OK
 *    - ESP_ERR_NVS_NOT_FOUND
 */
esp_err_t espnow_erase_dec_key(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
