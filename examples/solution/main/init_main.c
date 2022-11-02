/* Solution Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_utils.h"
#include "espnow.h"

#if CONFIG_ESPNOW_INITIATOR
#include "initiator.h"
#elif CONFIG_ESPNOW_RESPONDER
#include "responder.h"
#endif

#if CONFIG_WIFI_PROV
#include "wifi_prov.h"
#else
static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* Add ap netif if start WiFi softap */
    // esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}
#endif

void init()
{
    esp_storage_init();

    /* WiFi init and set config */
#if CONFIG_WIFI_PROV
    wifi_prov_init();
#else
    wifi_init();
#endif

    /**
     * @brief ESPNOW init
     */
    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.qsize = CONFIG_ESPNOW_QUEUE_SIZE;
#ifdef CONFIG_ESPNOW_SECURITY
    espnow_config.sec_enable = 1;
#endif
    espnow_init(&espnow_config);

    /* Example register event handler */
#if CONFIG_ESPNOW_INITIATOR
    espnow_initiator_reg();
#elif CONFIG_ESPNOW_RESPONDER
    espnow_responder_reg();
#endif
}
