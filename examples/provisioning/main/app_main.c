/* Provisioning Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "esp_utils.h"
#include "esp_storage.h"
#include "espnow.h"
#include "espnow_prov.h"

static const char *TAG = "app_main";

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#ifdef CONFIG_ESPNOW_PROV_RESPONDER
esp_err_t provisioning_responder()
{
    esp_err_t ret = ESP_OK;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    espnow_addr_t initator_addr = {0};
    espnow_prov_initator_t initator_info   = {0};
    espnow_prov_responder_t responder_info = {
        .product_id = "responder_test"
    };
    espnow_prov_wifi_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESPNOW_WIFI_SSID,
            .password = CONFIG_ESPNOW_WIFI_PASSWORD,
        },
    };

    ret = espnow_prov_responder_beacon(&responder_info, pdMS_TO_TICKS(30 * 1000));
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_prov_responder_beacon");

    for (int32_t start_ticks = xTaskGetTickCount(), recv_ticks = pdMS_TO_TICKS(30 * 1000); recv_ticks > 0;
            recv_ticks = pdMS_TO_TICKS(30 * 1000) - (xTaskGetTickCount() - start_ticks)) {
        ret = espnow_prov_responder_recv(initator_addr, &initator_info, &rx_ctrl, recv_ticks);
        ESP_ERROR_BREAK(ret == ESP_ERR_TIMEOUT, "");

        /**
         * @brief Authenticate the device through the information of the initiator
         */
        ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s, Auth Mode: %d, device_secret: %s",
                 MAC2STR(initator_addr), rx_ctrl.channel, rx_ctrl.rssi,
                 initator_info.product_id, initator_info.device_name,
                 initator_info.auth_mode, initator_info.device_secret);

        ret = espnow_prov_responder_send(&initator_addr, 1, &wifi_config);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "<%s> espnow_prov_responder_add", esp_err_to_name(ret));
    }

    return ESP_OK;
}
#endif

#ifdef CONFIG_ESPNOW_PROV_INITATOR
static esp_err_t provisioning_initator()
{
    esp_err_t ret = ESP_OK;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    espnow_prov_initator_t initator_info = {
        .product_id = "initator_test",
    };
    espnow_addr_t responder_addr = {0};
    espnow_prov_responder_t responder_info = {0};
    espnow_prov_wifi_t wifi_config = {0};

    for (;;) {
        ret = espnow_prov_initator_scan(responder_addr, &responder_info, &rx_ctrl, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_prov_responder_beacon");

        ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s",
                 MAC2STR(responder_addr), rx_ctrl.channel, rx_ctrl.rssi,
                 responder_info.product_id, responder_info.device_name);

        ret = espnow_prov_initator_send(responder_addr, &initator_info);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_prov_responder_add", esp_err_to_name(ret));

        ret = espnow_prov_initator_recv(responder_addr, &wifi_config, &rx_ctrl, pdMS_TO_TICKS(3 * 1000));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_prov_initator_recv", esp_err_to_name(ret));

        ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, wifi_mode: %d, ssid: %s, password: %s, token: %s",
                 MAC2STR(responder_addr), rx_ctrl.channel, rx_ctrl.rssi,
                 wifi_config.mode, wifi_config.sta.ssid, wifi_config.sta.password, wifi_config.token);
        break;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, (wifi_config_t *)&wifi_config.sta));
    ESP_ERROR_CHECK(esp_wifi_connect());

    return ESP_OK;
}
#endif

void app_main()
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_storage_init();
    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

#ifdef CONFIG_ESPNOW_PROV_INITATOR
    provisioning_initator();
#elif CONFIG_ESPNOW_PROV_RESPONDER
    provisioning_responder();
#endif
}
