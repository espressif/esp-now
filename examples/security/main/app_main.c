/* Security Example

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

#include "esp_utils.h"
#include "esp_storage.h"
#include "espnow.h"
#include "espnow_security.h"
#include "espnow_security_handshake.h"

#include "driver/uart.h"

const char *pop_data = CONFIG_ESPNOW_SESSION_POP;

#define CONFIG_UART_PORT_NUM  UART_NUM_0
#define CONFIG_UART_BAUD_RATE 115200
#define CONFIG_UART_TX_IO     UART_PIN_NO_CHANGE
#define CONFIG_UART_RX_IO     UART_PIN_NO_CHANGE

#define CONFIG_RETRY_NUM      5

static const char *TAG = "app_main";

static void uart_initialize()
{
    uart_config_t uart_config = {
        .baud_rate = CONFIG_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    ESP_ERROR_CHECK(uart_param_config(CONFIG_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CONFIG_UART_PORT_NUM, CONFIG_UART_TX_IO, CONFIG_UART_RX_IO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_UART_PORT_NUM, 8 * ESPNOW_DATA_LEN, 8 * ESPNOW_DATA_LEN, 0, NULL, 0));
}

static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void uart_read_task(void *arg)
{
    esp_err_t ret  = ESP_OK;
    uint32_t count = 0;
    size_t size    = 0;
    uint8_t *data  = ESP_CALLOC(1, ESPNOW_SEC_PACKET_MAX_SIZE);

    ESP_LOGI(TAG, "Uart read handle task is running");

    espnow_frame_head_t frame_head = {
        .retransmit_count = CONFIG_RETRY_NUM,
        .broadcast        = true,
    };

    for (;;) {
        size = uart_read_bytes(CONFIG_UART_PORT_NUM, data, ESPNOW_SEC_PACKET_MAX_SIZE, pdMS_TO_TICKS(10));
        ESP_ERROR_CONTINUE(size <= 0, "");

        ret = espnow_send(ESPNOW_TYPE_DATA, ESPNOW_ADDR_BROADCAST, data, size, &frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        ESP_LOGI(TAG, "espnow_send, count: %d, size: %d, data: %s", count++, size, data);
        memset(data, 0, ESPNOW_DATA_LEN);
    }

    ESP_LOGI(TAG, "Uart handle task is exit");

    ESP_FREE(data);
    vTaskDelete(NULL);
}

static esp_err_t uart_write_handle(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    static uint32_t count = 0;

    ESP_LOGI(TAG, "espnow_recv, <%d> [" MACSTR "][%d][%d][%d]: %.*s", 
            count++, MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi, size, size, (char *)data);

    return ESP_OK;
}

void app_main()
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_storage_init();
    esp_event_loop_create_default();

    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.sec_enable = 1;
    espnow_init(&espnow_config);

    espnow_set_type(ESPNOW_TYPE_DATA, 1, uart_write_handle);

    /* uart initialization */
    uart_initialize();
    xTaskCreate(uart_read_task, "uart_read_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);

#ifdef CONFIG_ESPNOW_SEC_INITATOR
    uint8_t key_info[APP_KEY_LEN];
    if (espnow_get_key(key_info) != ESP_OK) {
        esp_fill_random(key_info, APP_KEY_LEN);
    }
    espnow_set_key(key_info);

    uint32_t start_time1 = xTaskGetTickCount();
    espnow_sec_result_t espnow_sec_result = {0};
    espnow_sec_responder_t *info_list = NULL;
    size_t num = 0;
    espnow_sec_initiator_scan(&info_list, &num, pdMS_TO_TICKS(3000));
    ESP_LOGW(TAG, "espnow wait security num: %d", num);

    if (num == 0) {
        ESP_FREE(info_list);
        return;
    }

    espnow_addr_t *dest_addr_list = ESP_MALLOC(num * ESPNOW_ADDR_LEN);

    for (size_t i = 0; i < num; i++) {
        memcpy(dest_addr_list[i], info_list[i].mac, ESPNOW_ADDR_LEN);
    }

    espnow_sec_initiator_scan_result_free();

    uint32_t start_time2 = xTaskGetTickCount();
    esp_err_t ret = espnow_sec_initiator_start(key_info, pop_data, dest_addr_list, num, &espnow_sec_result);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> espnow_sec_initator_start", esp_err_to_name(ret));

    ESP_LOGI(TAG, "App key is sent to the device to complete, Spend time: %dms, Scan time: %dms",
             (xTaskGetTickCount() - start_time1) * portTICK_RATE_MS, 
             (start_time2 - start_time1) * portTICK_RATE_MS);
    ESP_LOGI(TAG, "Devices security completed, successed_num: %d, unfinished_num: %d", 
             espnow_sec_result.successed_num, espnow_sec_result.unfinished_num);

EXIT:
    ESP_FREE(dest_addr_list);
    espnow_sec_initator_result_free(&espnow_sec_result);
#elif CONFIG_ESPNOW_SEC_RESPONDER
    uint8_t key_info[APP_KEY_LEN];
    /* If espnow_set_key succeed, sending and receiving will be in security mode */
    if (espnow_get_key(key_info) == ESP_OK) {
        espnow_set_key(key_info);
    }

    /* If responder handshake with initiator succeed, espnow_set_key will be executed again. */
    espnow_sec_responder_start(pop_data);
#endif
}
