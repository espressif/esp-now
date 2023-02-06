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

void app_espnow_responder_register();
void app_espnow_responder();

#ifdef CONFIG_APP_ESPNOW_PROVISION
esp_err_t app_espnow_prov_responder_start(void);
#endif

#ifdef __cplusplus
}
#endif