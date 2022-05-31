/* Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void espnow_initiator_reg();
void espnow_initiator();

#ifdef CONFIG_ESPNOW_PROVISION
esp_err_t provision_beacon_start(int32_t sec);
#endif

#ifdef CONFIG_ESPNOW_SECURITY
void initiator_sec_start();
#endif

#ifdef __cplusplus
}
#endif