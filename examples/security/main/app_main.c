/* Security Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#endif

#include "espnow.h"
#include "espnow_security.h"
#include "espnow_security_handshake.h"
#include "espnow_storage.h"
#include "espnow_utils.h"

#include "driver/uart.h"

const char *pop_data = CONFIG_APP_ESPNOW_SESSION_POP;

// You can modify these according to your boards.
#define UART_PORT_NUM  UART_NUM_0
#define UART_BAUD_RATE 115200
#define UART_TX_IO     UART_PIN_NO_CHANGE
#define UART_RX_IO     UART_PIN_NO_CHANGE

static const char *TAG = "app_main";
static bool s_sec_flag = false;

#ifdef CONFIG_APP_ESPNOW_SEC_RESPONDER
static void app_espnow_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (base != ESP_EVENT_ESPNOW) {
        return;
    }

    switch (id) {
    case ESP_EVENT_ESPNOW_SEC_OK:
        ESP_LOGI(TAG, "ESP_EVENT_ESPNOW_SEC_OK [" MACSTR "]", MAC2STR((uint8_t *)event_data));
        s_sec_flag = true;
        break;

    case ESP_EVENT_ESPNOW_SEC_FAIL:
        ESP_LOGI(TAG, "ESP_EVENT_ESPNOW_SEC_FAIL [" MACSTR "]", MAC2STR((uint8_t *)event_data));
        s_sec_flag = false;
        break;

    default:
        break;
    }
}
#endif

static void app_uart_read_task(void *arg)
{
    esp_err_t ret  = ESP_OK;
    uint32_t count = 0;
    size_t size    = 0;
    uint8_t *data  = ESP_CALLOC(1, ESPNOW_SEC_PACKET_MAX_SIZE);

    ESP_LOGI(TAG, "Uart read handle task is running");

    espnow_frame_head_t frame_head = {
        .retransmit_count = CONFIG_APP_ESPNOW_RETRY_NUM,
        .broadcast        = true,
        .security         = CONFIG_APP_ESPNOW_SEC_OPTION
    };

    for (;;) {
        size = uart_read_bytes(UART_PORT_NUM, data, ESPNOW_SEC_PACKET_MAX_SIZE, pdMS_TO_TICKS(10));
        ESP_ERROR_CONTINUE(size <= 0, "");

        frame_head.security = s_sec_flag;
#ifdef CONFIG_APP_ESNOW_STRESS_OPTION
        frame_head.ack = false;
        uint32_t stress_data = 0;
        while (stress_data != 0xFFFFFFFF) {
            stress_data ++;
            ret = espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, &stress_data, sizeof(uint32_t), &frame_head, 0);
            if (ret != ESP_OK) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, &stress_data, sizeof(uint32_t), ESP_LOG_ERROR);
                ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
#else
        ret = espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, data, size, &frame_head, portMAX_DELAY);
        ESP_ERROR_CONTINUE(ret != ESP_OK, "<%s> espnow_send", esp_err_to_name(ret));

        ESP_LOGI(TAG, "espnow_send, count: %" PRIu32 ", size: %u, %s data: %s", count++, size, s_sec_flag ? "ciphertext" : "plaintext", data);
        memset(data, 0, ESPNOW_DATA_LEN);
#endif
    }

    ESP_LOGI(TAG, "Uart handle task is exit");

    ESP_FREE(data);
    vTaskDelete(NULL);
}

static void app_uart_initialize()
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if SOC_UART_SUPPORT_REF_TICK
        .source_clk = UART_SCLK_REF_TICK,
#elif SOC_UART_SUPPORT_XTAL_CLK
        .source_clk = UART_SCLK_XTAL,
#endif
    };

    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_IO, UART_RX_IO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, 8 * ESPNOW_DATA_LEN, 8 * ESPNOW_DATA_LEN, 0, NULL, 0));

    xTaskCreate(app_uart_read_task, "app_uart_read_task", 4 * 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
}

static void app_wifi_init()
{
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static esp_err_t app_uart_write_handle(uint8_t *src_addr, void *data,
                                       size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);
#ifdef CONFIG_APP_ESNOW_STRESS_OPTION
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, size, ESP_LOG_INFO);
#else
    static uint32_t count = 0;

    ESP_LOGI(TAG, "espnow_recv, <%" PRIu32 "> [" MACSTR "][%d][%d][%u]: %.*s",
             count++, MAC2STR(src_addr), rx_ctrl->channel, rx_ctrl->rssi, size, size, (char *)data);
#endif
    return ESP_OK;
}

void app_main()
{
    espnow_storage_init();

    app_wifi_init();
    app_uart_initialize();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.sec_enable = 1;
    espnow_init(&espnow_config);

    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, app_uart_write_handle);

#ifdef CONFIG_APP_ESPNOW_SEC_INITIATOR
    uint8_t key_info[APP_KEY_LEN];

    if (espnow_get_key(key_info) != ESP_OK) {
        esp_fill_random(key_info, APP_KEY_LEN);
    }
    espnow_set_key(key_info);
    espnow_set_dec_key(key_info);

    uint32_t start_time1 = xTaskGetTickCount();
    espnow_sec_result_t espnow_sec_result = {0};
    espnow_sec_responder_t *info_list = NULL;
    size_t num = 0;
    espnow_sec_initiator_scan(&info_list, &num, pdMS_TO_TICKS(3000));
    ESP_LOGW(TAG, "espnow wait security num: %u", num);

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
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> espnow_sec_initiator_start", esp_err_to_name(ret));
    s_sec_flag = true;

    ESP_LOGI(TAG, "App key is sent to the device to complete, Spend time: %" PRId32 "ms, Scan time: %" PRId32 "ms",
             (xTaskGetTickCount() - start_time1) * portTICK_PERIOD_MS,
             (start_time2 - start_time1) * portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Devices security completed, successed_num: %u, unfinished_num: %u",
             espnow_sec_result.successed_num, espnow_sec_result.unfinished_num);

EXIT:
    ESP_FREE(dest_addr_list);
    espnow_sec_initiator_result_free(&espnow_sec_result);
#elif CONFIG_APP_ESPNOW_SEC_RESPONDER
    uint8_t key_info[APP_KEY_LEN];

    /* If espnow_set_key succeed, sending and receiving will be in security mode */
    if (espnow_get_key(key_info) == ESP_OK) {
        espnow_set_key(key_info);
        espnow_set_dec_key(key_info);
    }

    /* If responder handshake with initiator succeed, espnow_set_key will be executed again. */
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, app_espnow_event_handler, NULL);
    espnow_sec_responder_start(pop_data);
#endif
}
