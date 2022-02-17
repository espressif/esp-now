/* Debug commands - declarations

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief  Register log command.
 */
void register_log();

/**
 * @brief  Register coredump command.
 */
void register_coredump();

/**
 * @brief  Register `command` command.
 */
void register_command();

/**
 * @brief  Register wifi config command.
 */
void register_wifi_config();

/**
 * @brief  Register wifi scan command.
 */
void register_wifi_scan();

/**
 * @brief  Register wifi sniffer command.
 */
void register_wifi_sniffer();

/**
 * @brief  Register sdcard command.
 */
void register_sdcard();

void register_scan();

void register_provisioning();
void register_control();
void register_ota();


void register_espnow_config();
void register_espnow_iperf();

/* register command `ping` */
void register_wifi_ping();

#ifdef __cplusplus
}
#endif /**< _cplusplus */
