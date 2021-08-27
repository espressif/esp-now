/* Get Start Example

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

#include "driver/uart.h"

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
    uint8_t *data  = ESP_CALLOC(1, ESPNOW_DATA_LEN);

    ESP_LOGI(TAG, "Uart read handle task is running");

    espnow_frame_head_t frame_head = {
        .retransmit_count = CONFIG_RETRY_NUM,
        .broadcast        = true,
    };

    for (;;) {
        size = uart_read_bytes(CONFIG_UART_PORT_NUM, data, ESPNOW_DATA_LEN, pdMS_TO_TICKS(10));
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

static void uart_write_task(void *arg)
{
    esp_err_t ret  = ESP_OK;
    uint32_t count = 0;
    char *data     = ESP_MALLOC(ESPNOW_DATA_LEN);
    size_t size   = ESPNOW_DATA_LEN;
    uint8_t addr[ESPNOW_ADDR_LEN] = {0};
    wifi_pkt_rx_ctrl_t rx_ctrl    = {0};

    ESP_LOGI(TAG, "Uart write task is running");

    for (;;) {
        ret = espnow_recv(ESPNOW_TYPE_DATA, addr, data, &size, &rx_ctrl, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s>", esp_err_to_name(ret));

        ESP_LOGI(TAG, "espnow_recv, <%d> [" MACSTR "][%d][%d][%d]: %.*s", 
                count++, MAC2STR(addr), rx_ctrl.channel, rx_ctrl.rssi, size, size, data);
        // uart_write_bytes(CONFIG_UART_PORT_NUM, data, size);
    }

    ESP_LOGW(TAG, "Uart send task is exit");

    ESP_FREE(data);
    vTaskDelete(NULL);
}

void app_main()
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_storage_init();
    esp_event_loop_create_default();

    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.qsize.data      = 64;
    espnow_init(&espnow_config);

    /* uart initialization */
    uart_initialize();
    xTaskCreate(uart_read_task, "uart_read_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(uart_write_task, "uart_write_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
}
