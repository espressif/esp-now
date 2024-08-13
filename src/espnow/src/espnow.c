/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_now.h"
#include "esp_log.h"
#include "espnow.h"
#include "esp_crc.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#include "esp_random.h"
#else
#include "esp_system.h"
#endif

#include "espnow_security.h"

#define SEND_CB_OK                      BIT0
#define SEND_CB_FAIL                    BIT1

#define ESPNOW_MSG_CACHE                32

#ifndef CONFIG_ESPNOW_VERSION
#define ESPNOW_VERSION                  2
#else
#define ESPNOW_VERSION                  CONFIG_ESPNOW_VERSION
#endif

#define SEND_DELAY_UNIT_MSECS         2

#if CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM
#define MAX_BUFFERED_NUM              (CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM / 2)     /* Not more than CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM */
#elif CONFIG_ESP32_WIFI_STATIC_TX_BUFFER_NUM
#define MAX_BUFFERED_NUM              (CONFIG_ESP32_WIFI_STATIC_TX_BUFFER_NUM / 2)     /* Not more than CONFIG_ESP32_WIFI_STATIC_TX_BUFFER_NUM */
#endif

/* Event source task related definitions */
ESP_EVENT_DEFINE_BASE(ESP_EVENT_ESPNOW);

typedef struct {
    uint8_t type;
    uint8_t group_id[6];
    uint8_t addrs_num;
    uint8_t addrs_list[0][6];
} espnow_group_info_t;
typedef struct {
    uint16_t frame_head;
    uint16_t duration;
    uint8_t destination_address[6];
    uint8_t source_address[6];
    uint8_t broadcast_address[6];
    uint16_t sequence_control;

    uint8_t category_code;
    uint8_t organization_identifier[3]; // 0x18fe34
    uint8_t random_values[4];
    struct {
        uint8_t element_id;                 // 0xdd
        uint8_t lenght;                     //
        uint8_t organization_identifier[3]; // 0x18fe34
        uint8_t type;                       // 4
        uint8_t version;
        uint8_t body[0];
    } vendor_specific_content;
} __attribute__((packed)) espnow_frame_format_t;

typedef struct {
    uint8_t type    : 4;
    uint8_t version : 2;
    uint8_t         : 2;
    uint8_t size;
    espnow_frame_head_t frame_head;
    uint8_t dest_addr[6];
    uint8_t src_addr[6];
    uint8_t payload[0];
} __attribute__((packed)) espnow_data_t;

/**
 * @brief Receive data packet temporarily store in queue
 */
typedef struct {
    wifi_pkt_rx_ctrl_t rx_ctrl; /**< metadata header */
    espnow_data_t data;
} espnow_pkt_t;

typedef enum {
    ESPNOW_EVENT_SEND_ACK,
    ESPNOW_EVENT_RECV_ACK,
    ESPNOW_EVENT_FORWARD,
    ESPNOW_EVENT_RECEIVE,
    ESPNOW_EVENT_STOP,
} espnow_msg_id_t;

typedef struct {
    espnow_msg_id_t msg_id;
    size_t data_len;
    void *data;
    void *handle;
} espnow_event_ctx_t;

static const char *TAG                  = "espnow";
static bool g_set_channel_flag          = true;
static espnow_config_t *g_espnow_config = NULL;
static espnow_sec_t *g_espnow_sec = NULL, *g_espnow_dec = NULL;
static EventGroupHandle_t g_event_group = NULL;
static QueueHandle_t g_espnow_queue = NULL;
static QueueHandle_t g_ack_queue = NULL;
static uint32_t g_buffered_num;
static uint8_t g_espnow_sec_key[APP_KEY_LEN] = {0}, g_espnow_dec_key[APP_KEY_LEN] = {0};
static bool g_read_from_nvs = true, g_read_dec_from_nvs = true;

static struct {
    uint8_t type;
    uint16_t magic;
} __attribute__((packed)) g_msg_magic_cache[ESPNOW_MSG_CACHE] = {0}, g_msg_magic_sec_cache[ESPNOW_MSG_CACHE] = {0};

static espnow_addr_t ESPNOW_ADDR_SELF       = {0};
const espnow_addr_t ESPNOW_ADDR_NONE        = {0};
const espnow_addr_t ESPNOW_ADDR_BROADCAST   = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0XFF};
const espnow_group_t ESPNOW_ADDR_GROUP_OTA  =  {'O', 'T', 'A', 0x0, 0x0, 0x0};
const espnow_group_t ESPNOW_ADDR_GROUP_PROV = {'P', 'R', 'O', 'V', 0x0, 0x0};
const espnow_group_t ESPNOW_ADDR_GROUP_SEC = {'S', 'E', 'C', 0x0, 0x0, 0x0};
static uint8_t g_msg_magic_cache_next = 0;
static espnow_frame_head_t g_espnow_frame_head_default = ESPNOW_FRAME_CONFIG_DEFAULT();

wifi_country_t g_self_country = {0};
static SemaphoreHandle_t g_send_lock = NULL;

typedef struct espnow_recv_handle {
    espnow_data_type_t type;
    bool enable;
    handler_for_data_t handle;
} espnow_recv_handle_t;

/* Keep the type order same with espnow_data_type_t */
static espnow_recv_handle_t g_recv_handle[ESPNOW_DATA_TYPE_MAX];

static bool queue_over_write(espnow_msg_id_t msg_id, const void *const data, size_t data_len, void *arg, TickType_t xTicksToWait)
{
    if (msg_id == ESPNOW_EVENT_RECV_ACK) {
        if (!g_ack_queue) {
            return false;
        }
        return xQueueSend(g_ack_queue, &data, xTicksToWait);
    } else {
        if (!g_espnow_queue) {
            return false;
        }

        espnow_event_ctx_t espnow_event = {
            .msg_id = msg_id,
            .data_len = data_len,
            .data = (void *)data,
            .handle = arg
        };

        return xQueueSend(g_espnow_queue, &espnow_event, xTicksToWait);
    }
}

/**< callback function of receiving ESPNOW data */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int size)
#else
void espnow_recv_cb(const uint8_t *addr, const uint8_t *data, int size)
#endif
{
    espnow_data_t *espnow_data = (espnow_data_t *)data;
    wifi_pkt_rx_ctrl_t *rx_ctrl = NULL;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 1)
    uint8_t * addr = recv_info->src_addr;
    rx_ctrl = recv_info->rx_ctrl;
#else
    wifi_promiscuous_pkt_t *promiscuous_pkt = (wifi_promiscuous_pkt_t *)(data - sizeof(wifi_pkt_rx_ctrl_t) - sizeof(espnow_frame_format_t));
    rx_ctrl = &promiscuous_pkt->rx_ctrl;
#endif

    ESP_LOG_BUFFER_HEXDUMP(TAG, data, size, ESP_LOG_DEBUG);
    ESP_LOGD(TAG, "[%s, %d], " MACSTR ", rssi: %d, size: %d, total: %d - %d, type: %d, addr: %02x, g_msg_magic_cache_next: %d",
             __func__, __LINE__, MAC2STR(addr), rx_ctrl->rssi, size, espnow_data->size, sizeof(espnow_data_t), espnow_data->type, addr[5], g_msg_magic_cache_next);

    /**< Filter ESP-NOW packets not generated by this project */
    if (espnow_data->version != ESPNOW_VERSION || (espnow_data->type >= ESPNOW_DATA_TYPE_MAX)
            || size != espnow_data->size + sizeof(espnow_data_t)
            || ESPNOW_ADDR_IS_SELF(espnow_data->src_addr)) {
        ESP_LOGD(TAG, "Receive cb args error, recv_addr: "MACSTR", src_addr: " MACSTR ", data: %p, size: %d",
                 MAC2STR(addr), MAC2STR(espnow_data->src_addr), data, size);
        return ;
    }

    espnow_frame_head_t *frame_head = &espnow_data->frame_head;

    /**< Data does not need to be forwarded */
    if (!g_recv_handle[espnow_data->type].enable
            && (!g_espnow_config->forward_enable || !frame_head->broadcast || !frame_head->forward_ttl)) {
        return ;
    }

    /**< Channel filtering */
    if (frame_head->filter_adjacent_channel && frame_head->channel != rx_ctrl->channel) {
        ESP_LOGD(TAG, "Filter adjacent channels, %d != %d", frame_head->channel, rx_ctrl->channel);
        return ;
    }

    /**< Rssi filtering */
    if (frame_head->filter_weak_signal && frame_head->forward_rssi > rx_ctrl->rssi) {
        ESP_LOGD(TAG, "Filter weak signal strength, %d > %d", frame_head->forward_rssi, rx_ctrl->rssi);
        return ;
    }

    /* Security filtering*/
    if (g_espnow_config && !g_espnow_config->sec_enable && frame_head->security) {
        ESP_LOGD(TAG, "Filter security frame");
        return;
    }

    if (g_recv_handle[espnow_data->type].enable
            && espnow_data->type != ESPNOW_DATA_TYPE_ACK && espnow_data->type != ESPNOW_DATA_TYPE_GROUP
            && frame_head->ack && ESPNOW_ADDR_IS_SELF(espnow_data->dest_addr)) {
        espnow_data_t *ack_data = ESP_CALLOC(1, sizeof(espnow_data_t));
        if (!ack_data) {
            return;
        }
        ack_data->version = ESPNOW_VERSION;
        ack_data->type  = ESPNOW_DATA_TYPE_ACK;
        ack_data->size  = 0;
        memcpy(&ack_data->frame_head, frame_head, sizeof(espnow_frame_head_t));
        memcpy(ack_data->src_addr, ESPNOW_ADDR_SELF, 6);
        memcpy(ack_data->dest_addr, espnow_data->src_addr, 6);

        ack_data->frame_head.retransmit_count = 1;
        ack_data->frame_head.broadcast = 1;

        if (!g_espnow_queue || queue_over_write(ESPNOW_EVENT_SEND_ACK, ack_data, sizeof(espnow_data_t), NULL, g_espnow_config->send_max_timeout) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send event queue failed", __func__, __LINE__);
            ESP_FREE(ack_data);
        }
    }

    if (!frame_head->security) {
        for (size_t i = 0, index = g_msg_magic_cache_next; i < ESPNOW_MSG_CACHE;
                i++, index = (g_msg_magic_cache_next + i) % ESPNOW_MSG_CACHE) {
            if (g_msg_magic_cache[index].type == espnow_data->type
                    && g_msg_magic_cache[index].magic == frame_head->magic) {
                return ;
            }
        }
    } else {
        for (size_t i = 0, index = g_msg_magic_cache_next; i < ESPNOW_MSG_CACHE;
                i++, index = (g_msg_magic_cache_next + i) % ESPNOW_MSG_CACHE) {
            if (g_msg_magic_sec_cache[index].type == espnow_data->type
                    && g_msg_magic_sec_cache[index].magic == frame_head->magic) {
                return ;
            }
        }
    }
#if CONFIG_IDF_TARGET_ESP32C6
    ESP_LOGD(TAG, "[%s, %d]: " MACSTR ", rssi: %d, channel: %d/%d, size: %d, %s, magic: 0x%x, ack: %d",
             __func__, __LINE__, MAC2STR(espnow_data->dest_addr), rx_ctrl->rssi, rx_ctrl->channel,
             rx_ctrl->second, espnow_data->size, espnow_data->payload,
             frame_head->magic, espnow_data->frame_head.ack);
#else
    ESP_LOGD(TAG, "[%s, %d]: " MACSTR ", rssi: %d, channel: %d/%d, size: %d, %s, magic: 0x%x, ack: %d",
             __func__, __LINE__, MAC2STR(espnow_data->dest_addr), rx_ctrl->rssi, rx_ctrl->channel,
             rx_ctrl->secondary_channel, espnow_data->size, espnow_data->payload,
             frame_head->magic, espnow_data->frame_head.ack);
#endif

    if (!g_recv_handle[espnow_data->type].enable) {
        goto FORWARD_DATA;
    }

    if (espnow_data->type == ESPNOW_DATA_TYPE_ACK) {
        if (!ESPNOW_ADDR_IS_SELF(espnow_data->dest_addr)) {
#ifdef CONFIG_ESPNOW_DATA_FAST_ACK
            if (g_recv_handle[ESPNOW_DATA_TYPE_ACK].handle) {
                g_recv_handle[ESPNOW_DATA_TYPE_ACK].handle(espnow_data->src_addr, (void *)frame_head, sizeof(espnow_frame_head_t), NULL);
            }
#endif
            goto FORWARD_DATA;
        }

        ESP_LOGD(TAG, ">[%s, %d]: broadcast: %d, dest_addr: " MACSTR, __func__, __LINE__, frame_head->broadcast,
                 MAC2STR(espnow_data->dest_addr));

        uint32_t *magic = ESP_MALLOC(sizeof(uint32_t));
        *magic = frame_head->magic;

        if (!g_ack_queue || queue_over_write(ESPNOW_EVENT_RECV_ACK, magic, sizeof(uint32_t), NULL, g_espnow_config->send_max_timeout) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send event queue failed", __func__, __LINE__);
            ESP_FREE(magic);
            return ;
        };

        goto EXIT;
    } else if (espnow_data->type == ESPNOW_DATA_TYPE_GROUP) {
        espnow_group_info_t *group_info = (espnow_group_info_t *) espnow_data->payload;
        bool set_group_flag = false;

        ESP_LOGD(TAG, ">1.2< group_id: " MACSTR ", dest_addr: " MACSTR,
                 MAC2STR(group_info->group_id), MAC2STR(espnow_data->dest_addr));
        ESP_LOGD(TAG, ">1.2< addrs_num: %d, dest_addr: " MACSTR, group_info->addrs_num,
                 MAC2STR(group_info->addrs_list[0]));

        if (group_info->addrs_num == 1 && ESPNOW_ADDR_IS_BROADCAST(group_info->addrs_list[0])) {
            set_group_flag = true;
        } else {
            if (espnow_data->size < (sizeof(espnow_group_info_t) + group_info->addrs_num * ESPNOW_ADDR_LEN)) {
                ESP_LOGD(TAG, "[%s, %d] The size %d of the data must match with total size for addrs_num: %d", __func__, __LINE__, espnow_data->size, group_info->addrs_num);
                return;
            }

            for (size_t i = 0; i < group_info->addrs_num; i++) {
                if (ESPNOW_ADDR_IS_SELF(group_info->addrs_list[i])) {
                    set_group_flag = true;
                    break;
                }
            }
        }

        if (set_group_flag) {
            if (group_info->type) {
                espnow_add_group(group_info->group_id);
            } else {
                espnow_del_group(group_info->group_id);
            }
        }
    } else {
        if (!frame_head->group && frame_head->broadcast && !ESPNOW_ADDR_IS_BROADCAST(espnow_data->dest_addr)
                && !ESPNOW_ADDR_IS_SELF(espnow_data->dest_addr)) {
            goto FORWARD_DATA;
        } else if (frame_head->group && !espnow_is_my_group(espnow_data->dest_addr)) {
            ESP_LOGD(TAG, "[%s, %d]: group_num: %d, group_id: " MACSTR,
                     __func__, __LINE__, espnow_get_group_num(), MAC2STR(espnow_data->dest_addr));
            goto FORWARD_DATA;
        }

        espnow_pkt_t *q_data = ESP_MALLOC(sizeof(espnow_pkt_t) + espnow_data->size);
        memcpy(&q_data->rx_ctrl, rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
        memcpy(&q_data->data, espnow_data, size);

        if (frame_head->channel && frame_head->channel != ESPNOW_CHANNEL_ALL) {
            q_data->rx_ctrl.channel = frame_head->channel;
        }

        if (queue_over_write(ESPNOW_EVENT_RECEIVE, q_data, sizeof(espnow_pkt_t) + espnow_data->size, NULL, g_espnow_config->send_max_timeout) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send event queue failed", __func__, __LINE__);
            ESP_FREE(q_data);
            return ;
        }
    }

FORWARD_DATA:

    if (g_espnow_config->forward_enable && frame_head->forward_ttl > 0 && frame_head->broadcast
            && frame_head->forward_rssi <= rx_ctrl->rssi && !ESPNOW_ADDR_IS_SELF(espnow_data->dest_addr)
            && !ESPNOW_ADDR_IS_SELF(espnow_data->src_addr)) {
        espnow_data_t *q_data = ESP_MALLOC(size);

        if (!q_data) {
            return ;
        }

        memcpy(q_data, espnow_data, size);

        if (frame_head->forward_ttl != ESPNOW_FORWARD_MAX_COUNT) {
            q_data->frame_head.forward_ttl--;
        }

        if (!g_espnow_queue || queue_over_write(ESPNOW_EVENT_FORWARD, q_data, size, NULL, g_espnow_config->send_max_timeout) != pdPASS) {
            ESP_LOGW(TAG, "[%s, %d] Send event queue failed", __func__, __LINE__);
            ESP_FREE(q_data);
            return ;
        }
    }

EXIT:
    g_msg_magic_cache_next = (g_msg_magic_cache_next + 1) % ESPNOW_MSG_CACHE;
    if (!frame_head->security) {
        g_msg_magic_cache[g_msg_magic_cache_next].type  = espnow_data->type;
        g_msg_magic_cache[g_msg_magic_cache_next].magic = frame_head->magic;
    } else {
        g_msg_magic_sec_cache[g_msg_magic_cache_next].type  = espnow_data->type;
        g_msg_magic_sec_cache[g_msg_magic_cache_next].magic = frame_head->magic;
    }
}

/**< callback function of sending ESPNOW data */
void espnow_send_cb(const uint8_t *addr, esp_now_send_status_t status)
{
    if (g_buffered_num) {
        g_buffered_num --;
    }

    if (!addr || !g_event_group) {
        ESP_LOGW(TAG, "Send cb args error, addr is NULL");
        return ;
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        xEventGroupSetBits(g_event_group, SEND_CB_OK);
    } else {
        xEventGroupSetBits(g_event_group, SEND_CB_FAIL);
    }
}

esp_err_t espnow_add_peer(const espnow_addr_t addr, const uint8_t *lmk)
{
    ESP_PARAM_CHECK(addr);

    /**< If peer exists, delete a peer from peer list */
    if (esp_now_is_peer_exist(addr)) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    esp_now_peer_info_t peer = {
        .ifidx = WIFI_IF_STA,
    };

    if (lmk) {
        peer.encrypt = true;
        memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    }

    memcpy(peer.peer_addr, addr, 6);

    /**< Add a peer to peer list */
    ret = esp_now_add_peer(&peer);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "Add a peer to peer list fail");

    return ESP_OK;
}

esp_err_t espnow_del_peer(const espnow_addr_t addr)
{
    ESP_PARAM_CHECK(addr);

    esp_err_t ret = ESP_OK;

    /**< If peer exists, delete a peer from peer list */
    if (esp_now_is_peer_exist(addr) && !ESPNOW_ADDR_IS_BROADCAST(addr)) {
        ret = esp_now_del_peer(addr);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_now_del_peer fail, ret: %d", ret);
    }

    return ESP_OK;
}

static esp_err_t espnow_send_process(int count, espnow_data_t *espnow_data, uint32_t wait_ticks, bool *ack)
{
    ESP_PARAM_CHECK(espnow_data);

    espnow_frame_head_t *frame_head = &espnow_data->frame_head;

    g_buffered_num ++;

    /* Wait send cb when max buffered num or ack is enable or unicast*/
    if (g_buffered_num >= MAX_BUFFERED_NUM || frame_head->ack || !frame_head->broadcast) {
        /**< For unicast packet, Waiting send complete ack from mac layer */
        /**< For broadcast packet, Waiting send ok from mac layer */
        EventBits_t uxBits = xEventGroupWaitBits(g_event_group, SEND_CB_OK | SEND_CB_FAIL,
                                pdTRUE, pdFALSE, MIN(wait_ticks, g_espnow_config->send_max_timeout));
        if ((uxBits & SEND_CB_OK) == SEND_CB_OK) {
            if (!frame_head->broadcast && !frame_head->ack && ack) {
                *ack = true;
                return ESP_OK;
            }
#ifdef CONFIG_ESPNOW_LIGHT_SLEEP
            if (ack) {
                *ack = true;
                return ESP_OK;
            }
#endif
        } else {
            return ESP_FAIL;
        }
    }

    if (frame_head->ack && !ESPNOW_ADDR_IS_BROADCAST(espnow_data->dest_addr) && ack) {
        uint32_t *ack_magic = NULL;

        /* retry backoff time (2,4,8,16,32,64,100,100,...)ms */
        uint32_t delay_ms = (count < 6 ? 1 << count : 50) * SEND_DELAY_UNIT_MSECS;

        do {
            vTaskDelay(pdMS_TO_TICKS(SEND_DELAY_UNIT_MSECS));
            while (g_ack_queue && xQueueReceive(g_ack_queue, &ack_magic, 0) == pdPASS) {
                if (*ack_magic == frame_head->magic) {
                    espnow_data->frame_head.ack = false;
                    ESP_FREE(ack_magic);
                    *ack = true;
                    return ESP_OK;
                }

                ESP_FREE(ack_magic);
            }

            delay_ms -= SEND_DELAY_UNIT_MSECS;
        } while (delay_ms > 0);

        return ESP_ERR_WIFI_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t espnow_send(espnow_data_type_t type, const espnow_addr_t dest_addr, const void *data,
                      size_t size, const espnow_frame_head_t *data_head, TickType_t wait_ticks)
{
    ESP_PARAM_CHECK(dest_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(type < ESPNOW_DATA_TYPE_MAX);
    ESP_PARAM_CHECK(size <= ESPNOW_DATA_LEN);
    ESP_ERROR_RETURN(!g_espnow_config, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    esp_err_t ret             = ESP_FAIL;
    TickType_t write_ticks    = 0;
    uint32_t start_ticks      = xTaskGetTickCount();
    uint8_t primary           = 0;
    wifi_second_chan_t second = 0;
    espnow_frame_head_t *frame_head = NULL;
    espnow_data_t *espnow_data = NULL;
    bool enc = false;

    if (g_espnow_config->sec_enable && (data_head ? data_head->security : g_espnow_frame_head_default.security)
        && type != ESPNOW_DATA_TYPE_ACK && type != ESPNOW_DATA_TYPE_FORWARD
        && type != ESPNOW_DATA_TYPE_SECURITY_STATUS && type != ESPNOW_DATA_TYPE_SECURITY) {
        ESP_ERROR_RETURN(!(g_espnow_sec && g_espnow_sec->state == ESPNOW_SEC_OVER), ESP_FAIL, "Security key is not set");
        size_t enc_len = 0;
        espnow_data = ESP_MALLOC(sizeof(espnow_data_t) + size + g_espnow_sec->tag_len + IV_LEN);
        if (!espnow_data) {
            ESP_LOGE(TAG, "Not enough memory!");
            return ret;
        }
        uint8_t key_info[APP_KEY_LEN];
        uint8_t iv_info[IV_LEN];

        ret = espnow_get_key(key_info);
        if (ret) {
            ESP_FREE(espnow_data);
            ESP_LOGE(TAG, "Get security key fail for encrypt, err_name: %s", esp_err_to_name(ret));
            return ret;
        }

        esp_fill_random(iv_info, IV_LEN);
        memcpy(key_info + KEY_LEN, iv_info, IV_LEN);
        espnow_set_key(key_info);

        ret = espnow_sec_auth_encrypt(g_espnow_sec, data, size, espnow_data->payload, size + g_espnow_sec->tag_len, &enc_len, g_espnow_sec->tag_len);
        espnow_data->size = enc_len + IV_LEN;
        if (ret == ESP_OK) {
            enc = 1;
            memcpy(espnow_data->payload + enc_len, iv_info, IV_LEN);
        } else {
            ESP_FREE(espnow_data);
            ESP_LOGE(TAG, "Security encrypt return error");
            return ret;
        }
    } else {
        espnow_data = ESP_MALLOC(sizeof(espnow_data_t) + size);
        if (!espnow_data) {
            ESP_LOGE(TAG, "Not enough memory!");
            return ret;
        }
        espnow_data->size = size;
        memcpy(espnow_data->payload, data, size);
    }

    if (data_head) {
        memcpy(&espnow_data->frame_head, data_head, sizeof(espnow_frame_head_t));
    } else {
        memcpy(&espnow_data->frame_head, &g_espnow_frame_head_default, sizeof(espnow_frame_head_t));
    }
    frame_head = &espnow_data->frame_head;

    if (enc) {
        frame_head->security = true;
    }

    if (!frame_head->magic) {
        frame_head->magic = esp_random();
    }

    if (!frame_head->broadcast && ESPNOW_ADDR_IS_BROADCAST(dest_addr)) {
        frame_head->broadcast = true;
    }

    if (frame_head->retransmit_count == 0) {
        frame_head->retransmit_count = 1;
    }

    espnow_data->version = ESPNOW_VERSION;
    espnow_data->type = type;
    memcpy(espnow_data->dest_addr, dest_addr, sizeof(espnow_data->dest_addr));
    memcpy(espnow_data->src_addr, ESPNOW_ADDR_SELF, sizeof(espnow_data->src_addr));

    ESP_LOGD(TAG, "[%s, %d] addr: " MACSTR", size: %d, count: %d, rssi: %d, data: %s, magic: 0x%x",
             __func__, __LINE__, MAC2STR(dest_addr), espnow_data->size, frame_head->retransmit_count,
             frame_head->forward_rssi, espnow_data->payload, frame_head->magic);

    /**< Wait for other tasks to be sent before send ESP-NOW data */
    if (xSemaphoreTake(g_send_lock, pdMS_TO_TICKS(wait_ticks)) != pdPASS) {
        ESP_FREE(espnow_data);
        return ESP_ERR_TIMEOUT;
    }

    ret = esp_wifi_get_channel(&primary, &second);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "esp_wifi_get_channel, err_name: %s", esp_err_to_name(ret));

    if (frame_head->channel == 0) {
        frame_head->channel = primary;
    } else if (frame_head->channel > 0 && frame_head->channel < ESPNOW_CHANNEL_ALL && frame_head->channel != primary) {
        if (g_set_channel_flag) {
            ESP_ERROR_GOTO(frame_head->channel >= g_self_country.schan + g_self_country.nchan, EXIT,
                "Can't set channel %d, not allowed in country %c%c%c.",
                frame_head->channel, g_self_country.cc[0], g_self_country.cc[1], g_self_country.cc[2]);
            ret = esp_wifi_set_channel(frame_head->channel, WIFI_SECOND_CHAN_NONE);
            ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "esp_wifi_set_channel, err_name: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGE(TAG, "Can't set channel %d, current is %d", frame_head->channel, primary);
            ret = ESP_FAIL;
            goto EXIT;
        }
    }

    const uint8_t *addr = (frame_head->broadcast) ? ESPNOW_ADDR_BROADCAST : dest_addr;

    for (int count = 0; count < frame_head->retransmit_count; ++count) {
        for (int i = 0; i == 0
                || (g_set_channel_flag && frame_head->channel == ESPNOW_CHANNEL_ALL && i < g_self_country.nchan); ++i) {

            if (g_set_channel_flag && frame_head->channel == ESPNOW_CHANNEL_ALL) {
                esp_wifi_set_channel(g_self_country.schan + i, WIFI_SECOND_CHAN_NONE);
            }

            xEventGroupClearBits(g_event_group, SEND_CB_OK | SEND_CB_FAIL);

            if (frame_head->ack) {
                count = 0;
            }

            do {
                ret = esp_now_send(addr, (uint8_t *)espnow_data, espnow_data->size + sizeof(espnow_data_t));

                if (ret == ESP_OK) {
                    bool ack = 0;
                    write_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                                xTaskGetTickCount() - start_ticks < wait_ticks ?
                                wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;
                    ret = espnow_send_process(count, espnow_data, write_ticks, &ack);
                    if (ret == ESP_OK && ack) {
                        goto EXIT;
                    }
                }

            } while (frame_head->ack && ++count < frame_head->retransmit_count);

            ESP_ERROR_CONTINUE(ret != ESP_OK, "[%s, %d] <%s> esp_now_send, channel: %d",
                               __func__, __LINE__, esp_err_to_name(ret), frame_head->channel == ESPNOW_CHANNEL_ALL ? g_self_country.schan + i : primary);
        }
    }

EXIT:

#ifdef CONFIG_ESPNOW_AUTO_RESTORE_CHANNEL
    if (g_set_channel_flag && frame_head->channel != primary) {
        esp_wifi_set_channel(primary, second);
    }
#endif

    xSemaphoreGive(g_send_lock);

    if (frame_head->ack && !ESPNOW_ADDR_IS_BROADCAST(dest_addr)) {
        uint32_t *ack_magic = NULL;
        write_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                      xTaskGetTickCount() - start_ticks < wait_ticks ?
                      wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;

        while (g_ack_queue && xQueueReceive(g_ack_queue, &ack_magic, MIN(write_ticks,
                             g_espnow_config->send_max_timeout)) == pdPASS) {
            if (*ack_magic == frame_head->magic) {
                ESP_FREE(espnow_data);
                ESP_FREE(ack_magic);
                return ESP_OK;
            }

            ESP_FREE(ack_magic);
        }

        ret = ESP_ERR_WIFI_TIMEOUT;
    }

    ESP_FREE(espnow_data);

    return ret;
}

esp_err_t espnow_set_group(const uint8_t addrs_list[][ESPNOW_ADDR_LEN], size_t addrs_num,
                            const uint8_t group_id[ESPNOW_ADDR_LEN], espnow_frame_head_t *data_head,
                            bool type, TickType_t wait_ticks)
{
    esp_err_t ret = ESP_OK;
    uint32_t start_ticks      = xTaskGetTickCount();

    espnow_data_t *espnow_data = ESP_MALLOC(addrs_num > 35 ? ESPNOW_PAYLOAD_LEN : sizeof(espnow_data_t) + sizeof(espnow_group_info_t) + addrs_num * ESPNOW_ADDR_LEN);
    espnow_frame_head_t *frame_head = &espnow_data->frame_head;
    espnow_group_info_t *group_info = (espnow_group_info_t *) espnow_data->payload;

    uint8_t primary           = 0;
    wifi_second_chan_t second = 0;

    espnow_data->version = ESPNOW_VERSION;
    espnow_data->type = ESPNOW_DATA_TYPE_GROUP;
    memcpy(espnow_data->dest_addr, ESPNOW_ADDR_BROADCAST, ESPNOW_ADDR_LEN);
    memcpy(espnow_data->src_addr, ESPNOW_ADDR_SELF, ESPNOW_ADDR_LEN);

    if (data_head) {
        memcpy(frame_head, data_head, sizeof(espnow_frame_head_t));
    } else {
        memcpy(frame_head, &g_espnow_frame_head_default, sizeof(espnow_frame_head_t));
    }

    if (!frame_head->magic) {
        frame_head->magic = esp_random();
    }

    ret = esp_wifi_get_channel(&primary, &second);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "esp_wifi_get_channel, err_name: %s", esp_err_to_name(ret));

    if (frame_head->channel == 0) {
        frame_head->channel = primary;
    } else if (frame_head->channel > 0 && frame_head->channel < ESPNOW_CHANNEL_ALL && frame_head->channel != primary) {
        if (g_set_channel_flag) {
            ret = esp_wifi_set_channel(frame_head->channel, WIFI_SECOND_CHAN_NONE);
            ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "esp_wifi_set_channel, err_name: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGE(TAG, "Can't set channel %d, current is %d", frame_head->channel, primary);
            ret = ESP_FAIL;
            goto EXIT;
        }
    }

    group_info->type = type;
    memcpy(group_info->group_id, group_id, ESPNOW_ADDR_LEN);

    for (int i = 0; addrs_num > 0; ++i) {
        size_t send_addrs_num = (addrs_num > 32) ? 32 : addrs_num;
        addrs_num -= send_addrs_num;
        espnow_data->size = sizeof(espnow_group_info_t) + send_addrs_num * ESPNOW_ADDR_LEN;
        group_info->addrs_num = send_addrs_num;
        memcpy(group_info->addrs_list, addrs_list + i * 32, send_addrs_num * ESPNOW_ADDR_LEN);
        frame_head->magic += i;

        for (int count = 0; count < frame_head->retransmit_count; ++count) {
            for (int i = 0; i == 0
                    || (frame_head->channel == ESPNOW_CHANNEL_ALL && i < g_self_country.nchan && g_set_channel_flag); ++i) {

                if (frame_head->channel == ESPNOW_CHANNEL_ALL && g_set_channel_flag) {
                    esp_wifi_set_channel(g_self_country.schan + i, WIFI_SECOND_CHAN_NONE);
                }

                ret = esp_now_send(ESPNOW_ADDR_BROADCAST, (uint8_t *)espnow_data, espnow_data->size + sizeof(espnow_data_t));

                if (ret == ESP_OK) {
                    TickType_t write_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                                xTaskGetTickCount() - start_ticks < wait_ticks ?
                                wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;
                    ret = espnow_send_process(count, espnow_data, write_ticks, NULL);
                }

                ESP_ERROR_CONTINUE(ret != ESP_OK, "[%s, %d] <%s> esp_now_send, channel: %d",
                                   __func__, __LINE__, esp_err_to_name(ret), g_self_country.schan + i);
            }
        }
    }

EXIT:

    if (frame_head->channel != primary && g_set_channel_flag) {
        esp_wifi_set_channel(primary, second);
    }

    ESP_FREE(espnow_data);
    return ESP_OK;
}

static esp_err_t espnow_recv_process(espnow_pkt_t *q_data)
{
    ESP_PARAM_CHECK(q_data);
    ESP_ERROR_RETURN(!g_espnow_config, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    esp_err_t ret        = 0;
    espnow_data_t *espnow_data = &q_data->data;
    espnow_frame_head_t *frame_head = &espnow_data->frame_head;
    uint8_t src_addr[6]     = {0};
    size_t size            = 0;
    uint8_t *data   = ESP_MALLOC(ESPNOW_PAYLOAD_LEN);
    wifi_pkt_rx_ctrl_t rx_ctrl = {0};

    ESP_LOGD(TAG, "[%s, %d], " MACSTR ", magic: 0x%04x, type: %d, size: %d, %s", __func__, __LINE__, MAC2STR(espnow_data->src_addr),
             frame_head->magic, espnow_data->type, espnow_data->size, espnow_data->payload);

    if (ret == ESP_OK) {
        /* Check security */
        if (frame_head->security) {
            if (g_espnow_config->sec_enable) {
                if (g_espnow_dec && g_espnow_dec->state == ESPNOW_SEC_OVER) {
                    uint8_t key_info[APP_KEY_LEN];

                    ret = espnow_get_dec_key(key_info);
                    if (ret) {
                        ESP_LOGE(TAG, "Get security key fail for decrypt, err_name: %s", esp_err_to_name(ret));
                        goto EXIT;
                    }
                    memcpy(key_info + KEY_LEN, espnow_data->payload + (espnow_data->size - IV_LEN), IV_LEN);
                    espnow_set_dec_key(key_info);

                    ret = espnow_sec_auth_decrypt(g_espnow_dec, espnow_data->payload, (espnow_data->size - IV_LEN), data, ESPNOW_PAYLOAD_LEN, &size, g_espnow_dec->tag_len);
                    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "espnow_sec_auth_decrypt, err_name: %s", esp_err_to_name(ret));
                } else {
                    ESP_LOGE(TAG, "Security key is not set");
                    goto EXIT;
                }
            } else {
                goto EXIT;
            }
        } else {
            size = espnow_data->size;
            memcpy(data, espnow_data->payload, espnow_data->size);
        }

        memcpy(src_addr, espnow_data->src_addr, 6);

        rx_ctrl = q_data->rx_ctrl;

        if (g_recv_handle[espnow_data->type].handle) {
            g_recv_handle[espnow_data->type].handle(src_addr, (void *)data, size, &rx_ctrl);
        }
    }

EXIT:
    ESP_FREE(q_data);
    ESP_FREE(data);
    return ret;
}

static esp_err_t espnow_send_forward(espnow_data_t *espnow_data)
{
    ESP_PARAM_CHECK(espnow_data);
    ESP_ERROR_RETURN(!g_espnow_config, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    esp_err_t ret        = 0;
    uint8_t primary           = 0;
    wifi_second_chan_t second = 0;

    ESP_LOGD(TAG, "[%s, %d], " MACSTR ", size: %d, %s", __func__, __LINE__,
            MAC2STR(espnow_data->src_addr), espnow_data->size, espnow_data->payload);

    espnow_frame_head_t *frame_head = &espnow_data->frame_head;
    const uint8_t *dest_addr = (frame_head->broadcast) ? ESPNOW_ADDR_BROADCAST : espnow_data->dest_addr;

#ifndef CONFIG_ESPNOW_DATA_FAST_ACK
    if (espnow_data->type == ESPNOW_DATA_TYPE_ACK && g_recv_handle[ESPNOW_DATA_TYPE_ACK].handle) {
        g_recv_handle[ESPNOW_DATA_TYPE_ACK].handle(espnow_data->src_addr, (void *)frame_head, sizeof(espnow_frame_head_t), NULL);
    }
#endif

    /**< Wait for other tasks to be sent before send ESP-NOW data */
    if (xSemaphoreTake(g_send_lock, g_espnow_config->send_max_timeout) != pdPASS) {
        ESP_LOGW(TAG, "Wait Sem fail");
        ESP_FREE(espnow_data);
        return ESP_ERR_TIMEOUT;
    }

    if (frame_head->channel == ESPNOW_CHANNEL_ALL && g_set_channel_flag && g_espnow_config->forward_switch_channel) {
        ESP_ERROR_CHECK(esp_wifi_get_channel(&primary, &second));
    }

    ESP_LOGD(TAG, "[%s, %d], " MACSTR ", total: %d, type: %d, magic: 0x%x", __func__, __LINE__,
            MAC2STR(espnow_data->src_addr),  espnow_data->size, espnow_data->type, frame_head->magic);

    uint32_t start_ticks      = xTaskGetTickCount();
    uint32_t max_ticks        = pdMS_TO_TICKS(g_espnow_config->send_max_timeout);
    for (int count = 0; !count || ((count < frame_head->retransmit_count) && (max_ticks > (xTaskGetTickCount() - start_ticks))); ++count) {
        for (int i = 0;  i == 0 || (frame_head->channel == ESPNOW_CHANNEL_ALL && i < g_self_country.nchan && g_set_channel_flag && g_espnow_config->forward_switch_channel); ++i) {

            if (frame_head->channel == ESPNOW_CHANNEL_ALL && g_set_channel_flag && g_espnow_config->forward_switch_channel) {
                esp_wifi_set_channel(g_self_country.schan + i, WIFI_SECOND_CHAN_NONE);
            }

            ret = esp_now_send(dest_addr, (uint8_t *)espnow_data, sizeof(espnow_data_t) + espnow_data->size);

            if (ret == ESP_OK) {
                ret = espnow_send_process(count, espnow_data, portMAX_DELAY, NULL);
            }


            ESP_ERROR_CONTINUE(ret != ESP_OK, "[%s, %d] <%s> esp_now_send, channel: %d",
                            __func__, __LINE__, esp_err_to_name(ret), g_self_country.schan + i);
        }
    }

    ESP_LOGD(TAG, "[%s, %d], " MACSTR ", size: %d, %s", __func__, __LINE__, MAC2STR(espnow_data->src_addr), espnow_data->size, espnow_data->payload);

    if (frame_head->channel == ESPNOW_CHANNEL_ALL && g_set_channel_flag && g_espnow_config->forward_switch_channel) {
        esp_wifi_set_channel(primary, second);
    }

    xSemaphoreGive(g_send_lock);
    ESP_FREE(espnow_data);

    return ret;
}

static void espnow_main_task(void *arg)
{
    espnow_event_ctx_t evt_data = { 0 };
    bool loop_continue = true;

    ESP_LOGI(TAG, "main task entry");

    if (g_espnow_config && g_espnow_config->qsize) {
        g_espnow_queue = xQueueCreate(g_espnow_config->qsize, sizeof(espnow_event_ctx_t));
        ESP_ERROR_GOTO(!g_espnow_queue, EXIT, "Create espnow event queue fail");

        if (g_recv_handle[ESPNOW_DATA_TYPE_ACK].enable) {
            g_ack_queue = xQueueCreate(4, sizeof(uint32_t *));
            ESP_ERROR_GOTO(!g_ack_queue, EXIT, "Create espnow ack queue fail");
        }
    }

    while (g_espnow_config && loop_continue) {
        if (xQueueReceive(g_espnow_queue, &evt_data, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (evt_data.msg_id == ESPNOW_EVENT_STOP) {
            loop_continue = false;
            continue;
        }

        if (evt_data.msg_id == ESPNOW_EVENT_SEND_ACK || evt_data.msg_id == ESPNOW_EVENT_FORWARD) {
            if (espnow_send_forward((espnow_data_t *)(evt_data.data)) != ESP_OK) {
                ESP_LOGD(TAG, "espnow_send_forward failed");
            }
            continue;
        }

        if (evt_data.msg_id == ESPNOW_EVENT_RECEIVE) {
            if (espnow_recv_process((espnow_pkt_t *)(evt_data.data)) != ESP_OK) {
                ESP_LOGD(TAG, "espnow_recv_process");
            }
            continue;
        }
    }

EXIT:
    if (g_espnow_queue) {
        while (xQueueReceive(g_espnow_queue, &evt_data, 0)) {
            ESP_FREE(evt_data.data);
        }

        vQueueDelete(g_espnow_queue);
        g_espnow_queue = NULL;
    }

    if (g_ack_queue) {
        vQueueDelete(g_ack_queue);
        g_ack_queue = NULL;
    }

    ESP_LOGI(TAG, "main task exit");
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    static bool s_ap_staconnected_flag = false;
    static bool s_sta_connected_flag   = false;

    switch (event_id) {
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
            ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
            s_ap_staconnected_flag = true;
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
            s_ap_staconnected_flag = false;
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:{
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG, "Connected to %s (BSSID: "MACSTR", Channel: %d)", event->ssid,
            MAC2STR(event->bssid), event->channel);
            s_sta_connected_flag = true;
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGI(TAG, "sta disconnect");
            s_sta_connected_flag = false;
            break;
        }

    default:
        break;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);

    if (mode == WIFI_MODE_APSTA || s_ap_staconnected_flag == true || s_sta_connected_flag == true) {
        g_set_channel_flag = false;
    } else {
        g_set_channel_flag = true;
    }
}

esp_err_t espnow_init(const espnow_config_t *config)
{
    ESP_LOGI(TAG, "esp-now Version: %d.%d.%d", ESP_NOW_VER_MAJOR, ESP_NOW_VER_MINOR, ESP_NOW_VER_PATCH);
    wifi_ap_record_t ap_info;

    ESP_PARAM_CHECK(config);

    if (g_espnow_config) {
        return ESP_OK;
    }

    g_espnow_config = ESP_MALLOC(sizeof(espnow_config_t));
    memcpy(g_espnow_config, config, sizeof(espnow_config_t));

    /**< Event group for espnow sent cb */
    g_event_group = xEventGroupCreate();
    ESP_ERROR_RETURN(!g_event_group, ESP_FAIL, "Create event group fail");

    g_send_lock = xSemaphoreCreateMutex();
    ESP_ERROR_RETURN(!g_send_lock, ESP_FAIL, "Create send semaphore mutex fail");

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));

    /* There may be chances that another Wi-Fi application has already connected to AP before ESP-NOW
     * registers wifi_event_handler. Check AP info here to avoid missing WIFI_EVENT_STA_CONNECTED action.
     */
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        ESP_LOGI(TAG, "device already connected to SSID: %s", ap_info.ssid);
        g_set_channel_flag = false;
    }

    if (config->sec_enable) {
        g_espnow_sec = ESP_MALLOC(sizeof(espnow_sec_t));
        g_espnow_dec = ESP_MALLOC(sizeof(espnow_sec_t));
        espnow_sec_init(g_espnow_sec);
        espnow_sec_init(g_espnow_dec);
    }

    uint32_t *enable = (uint32_t *)&config->receive_enable;
    for (int i = 0; i < ESPNOW_DATA_TYPE_MAX; ++i) {
        g_recv_handle[i].enable = (*enable) & BIT(i);
    }

    /**< Initialize ESPNOW function */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_set_pmk(config->pmk));

    espnow_add_peer(ESPNOW_ADDR_BROADCAST, NULL);

    esp_wifi_get_country(&g_self_country);
    esp_wifi_get_mac(ESP_IF_WIFI_STA, ESPNOW_ADDR_SELF);
    ESP_LOGI(TAG, "mac: " MACSTR ", version: %d", MAC2STR(ESPNOW_ADDR_SELF), ESPNOW_VERSION);

    ESP_LOGI(TAG, "Enable main task");
    xTaskCreate(espnow_main_task, "espnow_main", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);

    return ESP_OK;
}

esp_err_t espnow_deinit(void)
{
    ESP_ERROR_RETURN(!g_espnow_config, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    /**< De-initialize ESPNOW function */
    ESP_ERROR_CHECK(esp_now_unregister_recv_cb());
    ESP_ERROR_CHECK(esp_now_unregister_send_cb());
    ESP_ERROR_CHECK(esp_now_deinit());

    if (queue_over_write(ESPNOW_EVENT_STOP, NULL, 0, NULL, 0) != pdPASS) {
        ESP_LOGW(TAG, "[%s, %d] Send queue failed", __func__, __LINE__);
    }

    for (int i = 0; i < ESPNOW_DATA_TYPE_MAX; ++i) {
        g_recv_handle[i].enable = 0;
        g_recv_handle[i].handle = NULL;
    }

    if (g_espnow_config->sec_enable) {
        if (g_espnow_sec) {
            espnow_sec_deinit(g_espnow_sec);
            ESP_FREE(g_espnow_sec);
        }

        if (g_espnow_dec) {
            espnow_sec_deinit(g_espnow_dec);
            ESP_FREE(g_espnow_dec);
        }
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler));

    vSemaphoreDelete(g_send_lock);
    g_send_lock = NULL;

    vEventGroupDelete(g_event_group);
    g_event_group = NULL;

    ESP_FREE(g_espnow_config);
    g_espnow_config = NULL;

    return ESP_OK;
}

esp_err_t espnow_set_config_for_data_type(espnow_data_type_t type, bool enable, handler_for_data_t handle)
{
    ESP_ERROR_RETURN(!g_espnow_config, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");
    ESP_PARAM_CHECK(type >= ESPNOW_DATA_TYPE_ACK && type < ESPNOW_DATA_TYPE_MAX);

    if (g_recv_handle[type].enable != enable) {
        g_recv_handle[type].enable = enable;
    }

    if (enable) {
        g_recv_handle[type].handle = handle;
    } else {
        g_recv_handle[type].handle = NULL;
    }

    return ESP_OK;
}

esp_err_t espnow_get_config_for_data_type(espnow_data_type_t type, bool *enable)
{
    ESP_PARAM_CHECK(type >= ESPNOW_DATA_TYPE_ACK && type < ESPNOW_DATA_TYPE_MAX);

    *enable = g_recv_handle[type].enable;

    return ESP_OK;
}

esp_err_t espnow_set_key(uint8_t key_info[APP_KEY_LEN])
{
    ESP_PARAM_CHECK(g_espnow_sec);
    ESP_PARAM_CHECK(key_info);

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, key_info, APP_KEY_LEN, ESP_LOG_DEBUG);
    int ret = espnow_sec_setkey(g_espnow_sec, key_info);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_sec_setkey %x", ret);

    if (memcmp(key_info, g_espnow_sec_key, KEY_LEN) == 0)
        return ret;

    memcpy(g_espnow_sec_key, key_info, APP_KEY_LEN);
    ret = espnow_storage_set("key_info", key_info, APP_KEY_LEN);

    return ret;
}

esp_err_t espnow_get_key(uint8_t key_info[APP_KEY_LEN])
{
    ESP_PARAM_CHECK(key_info);

    if (g_read_from_nvs == false) {
        memcpy(key_info, g_espnow_sec_key, APP_KEY_LEN);
        return ESP_OK;
    }

    esp_err_t ret = espnow_storage_get("key_info", g_espnow_sec_key, APP_KEY_LEN);
    if (ret == ESP_OK) {
        memcpy(key_info, g_espnow_sec_key, APP_KEY_LEN);
        g_read_from_nvs = false;
    }
    return ret;
}

esp_err_t espnow_erase_key(void)
{
    g_read_from_nvs = true;
    memset(g_espnow_sec_key, 0, APP_KEY_LEN);
    return espnow_storage_erase("key_info");
}

esp_err_t espnow_set_dec_key(uint8_t key_info[APP_KEY_LEN])
{
    ESP_PARAM_CHECK(g_espnow_dec);
    ESP_PARAM_CHECK(key_info);

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, key_info, APP_KEY_LEN, ESP_LOG_DEBUG);
    int ret = espnow_sec_setkey(g_espnow_dec, key_info);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_sec_setkey %x", ret);

    if (memcmp(key_info, g_espnow_dec_key, KEY_LEN) == 0)
        return ret;

    memcpy(g_espnow_dec_key, key_info, APP_KEY_LEN);
    ret = espnow_storage_set("dec_key_info", key_info, APP_KEY_LEN);

    return ret;
}

esp_err_t espnow_get_dec_key(uint8_t key_info[APP_KEY_LEN])
{
    ESP_PARAM_CHECK(key_info);

    if (g_read_dec_from_nvs == false) {
        memcpy(key_info, g_espnow_dec_key, APP_KEY_LEN);
        return ESP_OK;
    }

    esp_err_t ret = espnow_storage_get("dec_key_info", g_espnow_dec_key, APP_KEY_LEN);
    if (ret == ESP_OK) {
        memcpy(key_info, g_espnow_dec_key, APP_KEY_LEN);
        g_read_dec_from_nvs = false;
    }
    return ret;
}

esp_err_t espnow_erase_dec_key(void)
{
    g_read_dec_from_nvs = true;
    memset(g_espnow_dec_key, 0, APP_KEY_LEN);
    return espnow_storage_erase("dec_key_info");
}
