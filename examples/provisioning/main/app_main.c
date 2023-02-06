/* Provisioning Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "espnow.h"
#include "espnow_prov.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

static const char *TAG = "app_main";

static void app_wifi_init()
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#ifdef CONFIG_APP_ESPNOW_PROV_INITIATOR
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#ifdef CONFIG_APP_ESPNOW_PROV_RESPONDER
static esp_err_t app_espnow_prov_recv_cb(uint8_t *src_addr, void *data,
        size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_prov_initiator_t *initiator_info = (espnow_prov_initiator_t *)data;
    /**
     * @brief Authenticate the device through the information of the initiator
     */
    ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s, Auth Mode: %d, device_secret: %s",
             MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi,
             initiator_info->product_id, initiator_info->device_name,
             initiator_info->auth_mode, initiator_info->device_secret);

    return ESP_OK;
}

static esp_err_t app_responder_init()
{
    esp_err_t ret = ESP_OK;
    espnow_prov_responder_t responder_info = {
        .product_id = "responder_test"
    };
    espnow_prov_wifi_t wifi_config = {
        .sta = {
            .ssid = CONFIG_APP_ESPNOW_WIFI_SSID,
            .password = CONFIG_APP_ESPNOW_WIFI_PASSWORD,
        },
    };

    ret = espnow_prov_responder_start(&responder_info, pdMS_TO_TICKS(30 * 1000), &wifi_config, app_espnow_prov_recv_cb);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_prov_responder_beacon");

    return ESP_OK;
}
#endif

#ifdef CONFIG_APP_ESPNOW_PROV_INITIATOR
static esp_err_t app_espnow_prov_recv_cb(uint8_t *src_addr, void *data,
        size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    espnow_prov_wifi_t *wifi_config = (espnow_prov_wifi_t *)data;

    ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, wifi_mode: %d, ssid: %s, password: %s, token: %s",
             MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi,
             wifi_config->mode, wifi_config->sta.ssid, wifi_config->sta.password, wifi_config->token);

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, (wifi_config_t *)&wifi_config->sta));
    ESP_ERROR_CHECK(esp_wifi_connect());
    return ESP_OK;
}

static esp_err_t app_initiator_init()
{
    esp_err_t ret = ESP_OK;
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};
    espnow_prov_initiator_t initiator_info = {
        .product_id = "initiator_test",
    };
    espnow_addr_t responder_addr = {0};
    espnow_prov_responder_t responder_info = {0};

    for (;;) {
        ret = espnow_prov_initiator_scan(responder_addr, &responder_info, &rx_ctrl, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "espnow_prov_responder_beacon");

        ESP_LOGI(TAG, "MAC: "MACSTR", Channel: %d, RSSI: %d, Product_id: %s, Device Name: %s",
                 MAC2STR(responder_addr), rx_ctrl.channel, rx_ctrl.rssi,
                 responder_info.product_id, responder_info.device_name);

        ret = espnow_prov_initiator_send(responder_addr, &initiator_info, app_espnow_prov_recv_cb, pdMS_TO_TICKS(3 * 1000));
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_prov_initiator_send", esp_err_to_name(ret));

        break;
    }

    return ESP_OK;
}
#endif

void app_main()
{
    espnow_storage_init();

    app_wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

#ifdef CONFIG_APP_ESPNOW_PROV_INITIATOR
    app_initiator_init();
#elif CONFIG_APP_ESPNOW_PROV_RESPONDER
    app_responder_init();
#endif
}
