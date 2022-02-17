/* Touch Button Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "hal/touch_sensor_ll.h"
#include "touch_element/touch_button.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "esp_wifi.h"


#include "espnow.h"
#include "espnow_ctrl.h"

#include "esp_storage.h"
#include "esp_utils.h"
#include "led_pwm.h"


#define TOUCH_BUTTON_NUM     1
static const char *TAG = "Touch panel";

#define MS_TO_RTC_SLOW_CLK(ms)    ((uint16_t)(((uint16_t)(ms) * 90000 ) / 1000))

static touch_button_handle_t button_handle[TOUCH_BUTTON_NUM]; //Button handler

/* Touch Sensor channel array */
static const touch_pad_t channel_array[TOUCH_BUTTON_NUM] = {
//    TOUCH_PAD_NUM10,
    TOUCH_PAD_NUM11,
//    TOUCH_PAD_NUM12,
};

/* Touch Sensor channel sensitivity array */
static const float channel_sens_array[TOUCH_BUTTON_NUM] = {
//    0.1,
    0.02,
//    0.1,
};


enum light_channel {
    CHANNEL_ID_RED   = 0,
    CHANNEL_ID_GREEN = 1,
    CHANNEL_ID_BLUE  = 2,
};


#define LEDC_LS_TIMER          LEDC_TIMER_1
#define LEDC_LS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE

void buzzer_driver_install(gpio_num_t buzzer_pin)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT,  //Resolution of PWM duty
        .freq_hz = 5000,                       //Frequency of PWM signal
        .speed_mode = LEDC_LS_MODE,            //Timer mode
        .timer_num = LEDC_LS_TIMER,            //Timer index
        .clk_cfg = LEDC_AUTO_CLK,              //Auto select the source clock (REF_TICK or APB_CLK or RTC_8M_CLK)
    };
    ledc_timer_config(&ledc_timer);
    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_LS_CH0_CHANNEL,
        .duty       = 4096,
        .gpio_num   = buzzer_pin,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER            //Let timer associate with LEDC channel (Timer1)
    };
    ledc_channel_config(&ledc_channel);
    ledc_fade_func_install(0);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
}

void buzzer_set_voice(bool en)
{
    uint32_t freq = en ? 5000 : 0;
    ledc_set_duty(LEDC_LS_MODE, LEDC_LS_CH0_CHANNEL, freq);
    ledc_update_duty(LEDC_LS_MODE, LEDC_LS_CH0_CHANNEL);
}


static void wifi_init()
{
    ESP_ERROR_CHECK(esp_netif_init());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_wifi_start());
}

void touch_element_setup(void)
{
    touch_ll_clear_trigger_status_mask();
    touch_ll_intr_clear(TOUCH_PAD_INTR_MASK_ALL);
    touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(touch_element_install(&global_config));
    touch_elem_waterproof_config_t waterproof_config = {
        .guard_channel = TOUCH_WATERPROOF_GUARD_NOUSE,
        .guard_sensitivity = 0.0F
    };
    ESP_ERROR_CHECK(touch_element_waterproof_install(&waterproof_config));
    ESP_LOGI(TAG, "Touch Element waterproof install");
    touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(touch_button_install(&button_global_config));
    ESP_LOGI(TAG, "Touch button installed");

    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        touch_button_config_t button_config = {
            .channel_num = channel_array[i],
            .channel_sens = channel_sens_array[i]
        };
        ESP_ERROR_CHECK(touch_button_create(&button_config, &button_handle[i]));
        ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i], TOUCH_ELEM_EVENT_ON_PRESS | TOUCH_ELEM_EVENT_ON_RELEASE | TOUCH_ELEM_EVENT_ON_LONGPRESS,
                        (void *)channel_array[i]));
        ESP_ERROR_CHECK(touch_button_set_longpress(button_handle[i], 2000));
    }

    ESP_ERROR_CHECK(touch_element_start());
}

void touch_element_sleep_start(touch_pad_t channel, uint32_t raw_sig)
{
    ESP_ERROR_CHECK(touch_element_stop());
    touch_ll_set_sleep_time(MS_TO_RTC_SLOW_CLK(100));
    ESP_ERROR_CHECK(esp_sleep_enable_touchpad_wakeup());

    ESP_ERROR_CHECK(touch_pad_sleep_channel_enable(channel, true));
    ESP_ERROR_CHECK(touch_pad_sleep_set_threshold(channel, raw_sig * channel_sens_array[0]));
    ESP_ERROR_CHECK(touch_element_start());
}

void app_main(void)
{
    uint32_t raw_sig = 0;
    uint32_t touch_press_time_ms = 0;
    esp_log_level_set("*", ESP_LOG_INFO);

    led_pwm_init(LEDC_TIMER_0, LEDC_LOW_SPEED_MODE, 1000);
    led_pwm_regist_channel(CHANNEL_ID_GREEN, GPIO_NUM_37);

    led_pwm_set_channel(CHANNEL_ID_GREEN, 255, 0);

    buzzer_driver_install(GPIO_NUM_42);
    buzzer_set_voice(1);

    esp_storage_init();

    uint32_t status[1] = {0};
    esp_storage_get("key_status", status, sizeof(status));

    wifi_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);

    touch_element_setup();

    buzzer_set_voice(0);

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TOUCHPAD
            || esp_storage_get("raw_sig", &raw_sig, sizeof(uint32_t)) != ESP_OK) {
        led_pwm_start_blink(CHANNEL_ID_GREEN, 255, 500, 0);
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_ERROR_CHECK(touch_pad_filter_read_smooth(channel_array[0], &raw_sig));
        ESP_LOGW(TAG, "raw_sig: %d", raw_sig);
        esp_storage_set("raw_sig", &raw_sig, sizeof(uint32_t));
    } else {
        esp_storage_get("raw_sig", &raw_sig, sizeof(uint32_t));

        uint32_t smooth = 0;

        for (int i = 0; i < 200; ++i) {
            if (touch_press_time_ms > 2000 && touch_press_time_ms < 5000) {
                buzzer_set_voice(i & 0x1);
            }

            touch_press_time_ms = 50 * (i + 1);
            vTaskDelay(pdMS_TO_TICKS(50));

            ESP_ERROR_CHECK(touch_pad_sleep_channel_read_smooth(channel_array[0], &smooth));
            ESP_LOGI(TAG, "smooth: %d, raw_sig: %d, raw_sig+ %0.2f%%: %f, time_ms: %dms",
                     smooth, raw_sig, channel_sens_array[0] * 100, raw_sig * (1 + channel_sens_array[0]), touch_press_time_ms);

            if (smooth < raw_sig * (1 + channel_sens_array[0])) {
                break;
            }
        }
    }

    buzzer_set_voice(0);

    if (touch_press_time_ms > 5000) {
        ESP_LOGW(TAG, "Unbind device");
        led_pwm_start_blink(CHANNEL_ID_GREEN, 255, 100, 0);
        espnow_ctrl_initiator_bind(ESPNOW_BUTTON_ATTRIBUTE, false);
    } else if (touch_press_time_ms > 2000) {
        ESP_LOGW(TAG, "Bind device");
        led_pwm_start_blink(CHANNEL_ID_GREEN, 255, 500, 0);
        espnow_ctrl_initiator_bind(ESPNOW_BUTTON_ATTRIBUTE, true);
    } else if (touch_press_time_ms) {
        uint32_t timestamp_start = esp_log_timestamp();
        espnow_ctrl_initiator_send(ESPNOW_BUTTON_ATTRIBUTE, ESPNOW_ATTRIBUTE_POWER, status[0]);
        ESP_LOGI(TAG, "data, <%u> responder_attribute: %d, responder_value_i: %d",
                 esp_log_timestamp() - timestamp_start, ESPNOW_ATTRIBUTE_POWER, status[0]);
        status[0] = !status[0];
        esp_storage_set("key_status", status, sizeof(status));
    }

    touch_element_sleep_start(channel_array[0], raw_sig);

    esp_wifi_stop();
    esp_wifi_deinit();

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_1);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_1, 0);
    gpio_hold_en(GPIO_NUM_1);

    esp_deep_sleep_start();
}
