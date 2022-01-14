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
#include "esp_utils.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#define ESPNOW_DATA_LEN                     (230)
#define ESPNOW_ADDR_LEN                     (6)
#define ESPNOW_DECLARE_COMMON_ADDR(addr)    extern const uint8_t addr[6];
#define ESPNOW_ADDR_IS_EMPTY(addr)          (((addr)[0] | (addr)[1] | (addr)[2] | (addr)[3] | (addr)[4] | (addr)[5]) == 0x0)
#define ESPNOW_ADDR_IS_BROADCAST(addr)      (((addr)[0] & (addr)[1] & (addr)[2] & (addr)[3] & (addr)[4] & (addr)[5]) == 0xFF)
#define ESPNOW_ADDR_IS_SELF(addr)           !memcmp(addr, ESPNOW_ADDR_SELF, 6)
#define ESPNOW_ADDR_IS_EQUAL(addr1, addr2)  !memcmp(addr1, addr2, 6)

typedef uint8_t (espnow_addr_t)[6];
typedef uint8_t (espnow_group_t)[6];

ESPNOW_DECLARE_COMMON_ADDR(ESPNOW_ADDR_NONE);
ESPNOW_DECLARE_COMMON_ADDR(ESPNOW_ADDR_BROADCAST);
ESPNOW_DECLARE_COMMON_ADDR(ESPNOW_ADDR_GROUP_OTA);
ESPNOW_DECLARE_COMMON_ADDR(ESPNOW_ADDR_GROUP_SEC);
ESPNOW_DECLARE_COMMON_ADDR(ESPNOW_ADDR_GROUP_PROV);

/**
 * @brief declaration of the task events family
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
#define ESPNOW_CHANNEL_ALL                   0x0f  /**< Full channel contracting */

#define ESPNOW_RETRANSMIT_MAX_COUNT          0x1f  /**< Maximum number of retransmissions */
#define ESPNOW_FORWARD_MAX_COUNT             0x1f  /**< Maximum number of retransmissions */

/**
 * @brief Initialize the configuration of espnow
 */
typedef struct {
    const uint8_t pmk[16];       /**< primary master key */
    bool forward_enable;         /**< Forward when packets are received */
    bool forward_switch_channel; /**< Forward data packet with exchange channel */
    uint8_t send_retry_num;      /**< Number of retransmissions */
    uint32_t send_max_timeout;   /**< Maximum timeout */
    struct {
        uint8_t ack;
        uint8_t forward;
        uint8_t group;
        uint8_t provisoning;
        uint8_t control_bind;
        uint8_t control_data;
        uint8_t ota_status;
        uint8_t ota_data;
        uint8_t debug_log;
        uint8_t debug_command;
        uint8_t data;
        uint8_t reserved;
    } qsize;                    /**< Size of packet buffer queue */
} espnow_config_t;

#define ESPNOW_INIT_CONFIG_DEFAULT() { \
    .pmk = "ESP_NOW", \
    .forward_enable = true, \
    .send_retry_num = 10, \
    .send_max_timeout = pdMS_TO_TICKS(3000),\
        .qsize = { \
                .ack           = 8, \
                .forward       = 8, \
                .group         = 8, \
                .provisoning   = 0, \
                .control_bind  = 0, \
                .control_data  = 0, \
                .ota_status    = 0, \
                .ota_data      = 0, \
                .debug_log     = 0, \
                .debug_command = 0, \
                .data          = 0, \
                .reserved      = 0, \
                }, \
    }
/**
 * @brief Divide espnnow data into multiple pipes
 */
typedef enum {
    ESPNOW_TYPE_ACK,           /**< For reliable data transmission */
    ESPNOW_TYPE_FORWARD,       /**< sed to forward packets in data */
    ESPNOW_TYPE_GROUP,         /**< Send a packet that sets the group type */
    ESPNOW_TYPE_PROV,          /**< network configuration packet */
    ESPNOW_TYPE_CONTROL_BIND,  /**< Configure network, practice fast network configuration */
    ESPNOW_TYPE_CONTROL_DATA,  /**< Configure network, practice fast network configuration*/
    ESPNOW_TYPE_OTA_STATUS,    /**< Rapid upgrade of batch Device */
    ESPNOW_TYPE_OTA_DATA,      /**< Rapid upgrade of batch Device */
    ESPNOW_TYPE_DEBUG_LOG,     /**< Equipment debugging */
    ESPNOW_TYPE_DEBUG_COMMAND, /**< Equipment debugging */
    ESPNOW_TYPE_DATA,          /**< User-defined use */
    ESPNOW_TYPE_SECURITY_STATUS,/**< Security status packet */
    ESPNOW_TYPE_SECURITY,      /**< Security handshake packet */
    ESPNOW_TYPE_RESERVED,      /**< reserved for other functiond */
    ESPNOW_TYPE_MAX,
} espnow_type_t;

typedef struct {
    uint16_t magic;                    /**< Unique identifier of each packet. Packets with the same identifier will be filtered. 0: a random number */
    uint8_t channel              : 4;  /**< Set the channel where the packet is sent, ESPNOW_CHANNEL_CURRENT or ESPNOW_CHANNEL_ALL */
    bool filter_adjacent_channel : 1;  /**< Because espnow is sent through HT20, it can receive packets from adjacent channels */
    bool filter_weak_signal      : 1;  /**< When the signal received by the receiving device is lower than forward_rssi frame_head data will be discarded */
    uint16_t                     : 5;  /**< reserved */

    /**
     * @brief Configure broadcast
     */
    bool broadcast              : 1;
    bool group                  : 1;  /**< Only the group set as broadcast transmission mode is valid */
    bool ack                    : 1;  /**< Wait for the receiving device to return ack to ensure transmission reliability */
    uint16_t retransmit_count   : 5;  /**< Too many packet retransmissions will lead to network congestion */
    uint8_t forward_ttl         : 5;  /**< Number of hops in data transfer */
    int8_t forward_rssi         : 8;  /**< When the data packet signal received by the receiving device is lower than forward_rssi, it will not be transferred,
                                           in order to avoid network congestion caused by packet transfer */
} __attribute__((packed)) espnow_frame_head_t;

#define ESPNOW_FRAME_CONFIG_DEFAULT() \
    { \
        .broadcast = true, \
        .retransmit_count = 10, \
    }

/**
 * @brief When used for unicast, add the target device
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_add_peer(const espnow_addr_t addr, const uint8_t *lmk);

/**
 * @brief When used for unicast, delete the target device
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_del_peer(const espnow_addr_t addr);

/**
 * @brief   Send ESPNOW data
 *
 * @param   type
 * @param   dest_addr
 * @param   size  The maximum length of data must be less than ESPNOW_DATA_LEN
 * @param   frame_config  If frame_config is NULL, Use ESPNOW_FRAME_CONFIG_DEFAULT configuration
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_send(espnow_type_t type, const espnow_addr_t dest_addr, const void *data,
                      size_t size, const espnow_frame_head_t *frame_config, TickType_t wait_ticks);


/**
 * @brief   Recive ESPNOW data
 *
 * @param   mac_addr peer MAC address
 * @param   data received data
 * @param   data_len length of received data
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_recv(espnow_type_t type, espnow_addr_t src_addr, void *data,
                      size_t *size, wifi_pkt_rx_ctrl_t *rx_ctrl, TickType_t wait_ticks);


/**
 * @brief De-initialize ESPNOW function
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_deinit(void);

/**
 * @brief Initialize ESPNOW function
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_init(const espnow_config_t *config);

/**
 * @brief Set the size of the buffer queue
 *
 * @param   size  Delete this queue if size is zero
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_set_qsize(espnow_type_t type, size_t size);

/**
 * @brief Get the size of the buffer queue
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_get_qsize(espnow_type_t type, size_t *size);

/**
 * @brief      Set group ID addresses
 *
 * @param[in]  addr  pointer to new group ID addresses
 * @param[in]  num  the number of group ID addresses
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_add_group(const espnow_group_t group_id);

/**
 * @brief      Delete group ID addresses
 *
 * @param[in]  addr  pointer to deleted group ID address
 * @param[in]  num  the number of group ID addresses
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
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
 * @param[out] addr  pointer to group ID addresses
 * @param[in]  num  the number of group ID addresses
 *
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_get_group_list(espnow_group_t *group_id_list, size_t num);

/**
 * @brief      Check whether the specified group address is my group
 *
 * @return     true/false
 */
bool espnow_is_my_group(const espnow_group_t group_id);

/**
 * @brief      Dynamically set the grouping of devices through commands
 *
 * @param   type
 * @param   dest_addr
 * @param   size  The maximum length of data must be less than ESPNOW_DATA_LEN
 * @param   frame_config  If frame_config is NULL, Use ESPNOW_FRAME_CONFIG_DEFAULT configuration
 * 
 * @return
 *    - ESP_OK
 *    - ESP_MESH_ERR_ARGUMENT
 */
esp_err_t espnow_send_group(const espnow_addr_t *addrs_list, size_t addrs_num,
                            const espnow_group_t group_id, espnow_frame_head_t *frame_head,
                            bool type, TickType_t wait_ticks);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
