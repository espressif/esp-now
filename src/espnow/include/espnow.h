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
#define ESPNOW_PAYLOAD_LEN                  (230)

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
#define ESP_EVENT_ESPNOW_RESERVED_BASE      0x500

/**
 * @brief The channel on which the device sends packets
 */
#define ESPNOW_CHANNEL_CURRENT               0x0   /**< Only in the current channel */
#define ESPNOW_CHANNEL_ALL                   0x0f  /**< All supported channels */

#define ESPNOW_RETRANSMIT_MAX_COUNT          0x1f  /**< Maximum number of retransmissions */
#define ESPNOW_FORWARD_MAX_COUNT             0x1f  /**< Maximum number of forwards */

/**
 * @brief Initialize the configuration of espnow
 */
typedef struct {
    const uint8_t pmk[16];                  /**< Primary master key */
    bool forward_enable             : 1;    /**< Forward when packets are received */
    bool forward_switch_channel     : 1;    /**< Forward data packet with exchange channel */
    bool sec_enable                 : 1;    /**< Encrypt ESP-NOW data payload when send and decrypt when receive */
    uint8_t reserved1               : 5;    /**< Reserved */
    uint8_t qsize;                          /**< Size of packet buffer queue */
    uint8_t send_retry_num;                 /**< Number of retransmissions */
    uint32_t send_max_timeout;              /**< Maximum timeout */
    struct {
        bool ack                    : 1;    /**< Enable or disable ACK */
        bool forward                : 1;    /**< Enable or disable forword */
        bool group                  : 1;    /**< Enable or disable group */
        bool provisoning            : 1;    /**< Enable or disable provisoning */
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
        uint32_t reserved2          : 18;   /**< Reserved */
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
                .provisoning   = 0, \
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
    bool ack                    : 1;  /**< Wait for the receiving device to return ack to ensure transmission reliability */
    uint16_t retransmit_count   : 5;  /**< Too many packet retransmissions will lead to network congestion */
    uint8_t forward_ttl         : 5;  /**< Number of hops in data transfer */
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
