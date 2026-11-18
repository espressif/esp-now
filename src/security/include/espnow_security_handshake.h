// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "espnow.h"
#include "espnow_utils.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Type of packet
 */
typedef enum {
    ESPNOW_SEC_TYPE_REQUEST,    /**< Request security information */
    ESPNOW_SEC_TYPE_INFO,       /**< Security information */
    ESPNOW_SEC_TYPE_HANDSHAKE,  /**< Handshake packet to get key */
    ESPNOW_SEC_TYPE_KEY,        /**< Packet containing app key */
    ESPNOW_SEC_TYPE_KEY_RESP,   /**< Response to confirm gotten the app key */
    ESPNOW_SEC_TYPE_REST,       /**< Reset security information. Unauthenticated and forgeable;
                                     ignored by the responder with CONFIG_ESPNOW_SEC_IGNORE_REST
                                     (recommended), otherwise clears sec_ver and the paired initiator */
} espnow_sec_type_t;

/**
 * @brief Minimum and recommended length of the Proof of Possession (PoP)
 *
 * The PoP is the only secret protecting the application key: a device spoofing
 * a responder can test PoP guesses offline and handshake attempts are not rate
 * limited. Use a per-deployment secret of at least ESPNOW_SEC_POP_RECOMMENDED_LEN
 * random characters, never the public example value `espnow_pop`. The library
 * does not enforce these limits (the start functions accept any non-empty
 * string); the application must check the length before calling them. The
 * `security -s` debug console command rejects a PoP shorter than
 * ESPNOW_SEC_POP_MIN_LEN and falls back to the public example PoP when -p is
 * omitted (debug use only).
 */
#define ESPNOW_SEC_POP_MIN_LEN          8
#define ESPNOW_SEC_POP_RECOMMENDED_LEN  16

/**
 * @brief Security version
 */
typedef enum {
    ESPNOW_SEC_VER_NONE,        /**< No security */
    ESPNOW_SEC_VER_V1_0,        /**< The default security version */
    ESPNOW_SEC_VER_V1_1,        /**< Used in the future */
} espnow_sec_ver_type_t;

/**
 * @brief Security information
 */
typedef struct espnow_sec_info_s {
    uint8_t type;               /**< ESPNOW_SEC_TYPE_REQUEST or ESPNOW_SEC_TYPE_INFO */
    uint8_t sec_ver;            /**< Security version */
    uint8_t client_mac[6];      /**< Mac address of initiator */
} espnow_sec_info_t;

/**
 * @brief Responder security information
 */
typedef struct espnow_sec_responder_s {
    uint8_t mac[6];             /**< Mac address of responder */
    int8_t rssi;                /**< Packet rssi */
    uint8_t channel;            /**< The channel of responder */
    uint8_t sec_ver;            /**< The security version of responder */
} espnow_sec_responder_t;

/**
 * @brief Handshake packet
 */
typedef struct espnow_sec_packet_s {
    uint8_t type;               /**< Type of packet, ESPNOW_SEC_TYPE_HANDSHAKE */
    uint8_t size;               /**< Size */
    uint8_t data[0];            /**< Message */
} ESPNOW_PACKED_STRUCT espnow_sec_packet_t;

/**
 * @brief List of device status during the security process
 */
typedef struct espnow_sec_result_s {
    size_t unfinished_num;          /**< The number of devices to be set key */
    espnow_addr_t *unfinished_addr; /**< MAC address of devices to be set key */

    size_t successed_num;           /**< The number of devices that succeeded to set key */
    espnow_addr_t *successed_addr;  /**< MAC address of devices that succeeded to set key */

    size_t requested_num;           /**< Reserved */
    espnow_addr_t *requested_addr;  /**< Reserved */
} espnow_sec_result_t;

/**
 * @brief  Root scans other node info
 *
 * @attention Only called at the root
 *
 * @param[out]  info_list  destination nodes of mac
 * @param[out]  num  number of scanned nodes
 * @param[in]  wait_ticks  the maximum scanning time in ticks
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_scan(espnow_sec_responder_t **info_list, size_t *num, TickType_t wait_ticks);

/**
 * @brief Free memory in the scan information list
 *
 * @return
 *    - ESP_OK
 */
esp_err_t espnow_sec_initiator_scan_result_free(void);

/**
 * @brief  Root sends security to other nodes
 *
 * @attention Only called at the root
 *
 * @note Trust boundary. The handshake is protocomm security1 over plaintext,
 *       unauthenticated frames:
 *       - Only the PoP protects the key; see ESPNOW_SEC_POP_MIN_LEN.
 *       - The KEY message is encrypted but not authenticated. While a responder's
 *         handshake is open (at most CONFIG_ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS, only
 *         while this function runs) a device spoofing the initiator address can
 *         inject a frame that the responder installs as a key nobody knows; the
 *         node may still appear in successed_addr. No key is disclosed.
 *       - A responder reports ESPNOW_SEC_VER_NONE after every reboot and accepts a
 *         new handshake.
 *       The handshake is a commissioning step, like Wi-Fi provisioning: run it in
 *       a controlled environment on an explicit action, never automatically on
 *       every boot; afterwards verify every node with a secured request and a
 *       secured reply (an ACK is plaintext and proves nothing); power-cycle a
 *       node that does not answer and run this function again for it. An
 *       injected KEY frame then costs one repeated commissioning step, nothing more.
 *
 * @param[in]  key_info  the security key info to sent to responder
 * @param[in]  pop_data  Proof of Possession (PoP) string; see the note on ESPNOW_SEC_POP_MIN_LEN
 * @param[in]  addrs_list  destination nodes of mac
 * @param[in]  addrs_num  number of destination nodes
 * @param[out]  res  must call espnow_sec_initiator_result_free to free memory
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_start(uint8_t key_info[APP_KEY_LEN], const char *pop_data, const uint8_t addrs_list[][6], size_t addrs_num,
                                    espnow_sec_result_t *res);

/**
 * @brief Stop Root to send security to other nodes
 *
 * @return
 *    - ESP_OK
 */
esp_err_t espnow_sec_initiator_stop();

/**
 * @brief Free memory in the results list
 *
 * @param[in]  result  pointer to device security status
 *
 * @return
 *    - ESP_OK
 *    - ESP_ERR_INVALID_ARG
 */
esp_err_t espnow_sec_initiator_result_free(espnow_sec_result_t *result);

/**
 * @brief Start security process
 *
 * @note Every rejected HANDSHAKE message (wrong PoP, malformed frame) makes
 *       protocomm print one or more ESP_LOGE lines that this component cannot
 *       rate-limit. The frames are unauthenticated, so while the responder waits
 *       for a key any device in range can trigger these lines at wire speed. If
 *       espnow_log writes to flash, keep its flash level above ERROR, or only
 *       enable flash logging after the handshake has completed.
 *
 * @note While no key is set, any device knowing the PoP can complete a handshake
 *       with this responder; see the trust-boundary note on espnow_sec_initiator_start.
 *
 * @note Design limitation: the handshake runs over plaintext, unauthenticated
 *       frames, so while this responder runs any device in range can hold the
 *       single handshake slot for CONFIG_ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS per
 *       attempt, inject a KEY frame or flood error logs, and after a reboot a new
 *       handshake is accepted again. The handshake is a commissioning step in a
 *       controlled environment, like Wi-Fi provisioning. Recommended usage: start
 *       on an explicit commissioning action (only when espnow_get_key() finds no
 *       key or a re-key is intended) and call espnow_sec_responder_stop() on
 *       ESP_EVENT_ESPNOW_SEC_OK or timeout (see the note on
 *       espnow_sec_responder_stop). Starting the responder at every boot
 *       or leaving it running after commissioning is not recommended and not
 *       protected. Within the recommended usage a device present during the
 *       commissioning window can at most delay or fail that commissioning run:
 *       no key is disclosed, nothing persistent is changed on a node that is
 *       already keyed, a node left with an injected key is detected by the
 *       verification step and commissioned again, and the effect ends when the
 *       window is closed.
 *
 * @param[in]  pop_data  Proof of Possession (PoP) string; see the note on ESPNOW_SEC_POP_MIN_LEN
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_responder_start(const char *pop_data);

/**
 * @brief Stop security process
 *
 * @note ESP_EVENT_ESPNOW_SEC_OK is posted after the key has been stored and the
 *       KEY_RESP has been encrypted and handed to espnow_send(), and this function
 *       waits for a SECURITY frame that is still being handled before it frees the
 *       protocomm session. It may therefore be called at any time from any task,
 *       including the ESP_EVENT_ESPNOW_SEC_OK or ESP_EVENT_ESPNOW_SEC_FAIL handler;
 *       it blocks for at most the time needed to answer the frame in progress.
 *       ESP_EVENT_ESPNOW_SEC_FAIL means the handshake was not completed: the
 *       responder is not marked as configured and keeps accepting a new handshake
 *       from the initiator without being restarted. If the app key had already
 *       been installed when the failure happened (KEY_RESP could not be encrypted
 *       or sent) it stays installed, as after a KEY_RESP lost on the air, and the
 *       retried handshake installs the key it carries.
 *
 * @return
 *    - ESP_OK
 *    - ESP_FAIL
 */
esp_err_t espnow_sec_responder_stop();

#ifdef __cplusplus
}
#endif /**< _cplusplus */
