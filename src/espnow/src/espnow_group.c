// Copyright 2021 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "espnow.h"
#include "esp_crc.h"
#include "esp_mac.h"

static const char *TAG = "espnow_group";

/**< Bound the group list: unauthenticated GROUP frames can add groups remotely. */
#ifndef CONFIG_ESPNOW_GROUP_COUNT_MAX
#define ESPNOW_GROUP_COUNT_MAX 32
#else
#define ESPNOW_GROUP_COUNT_MAX CONFIG_ESPNOW_GROUP_COUNT_MAX
#endif

typedef struct espnow_group_item {
    espnow_group_t id;
    SLIST_ENTRY(espnow_group_item) next;
} espnow_group_item_t;

static SLIST_HEAD(group_item_list_, espnow_group_item) g_group_item_list;

/**< Built-in groups bypass the cap and are never evicted. */
static bool espnow_group_is_builtin(const espnow_group_t group_id)
{
    return !memcmp(group_id, ESPNOW_ADDR_GROUP_OTA, 6)
           || !memcmp(group_id, ESPNOW_ADDR_GROUP_PROV, 6)
           || !memcmp(group_id, ESPNOW_ADDR_GROUP_SEC, 6);
}

esp_err_t espnow_add_group(const espnow_group_t group_id)
{
    espnow_group_item_t *item = NULL;
    size_t group_count = 0;

    SLIST_FOREACH(item, &g_group_item_list, next) {
        if (!memcmp(item->id, group_id, 6)) {
            return ESP_OK;
        }
        if (!espnow_group_is_builtin(item->id)) {
            group_count++;
        }
    }

    /**< Allocate the new item before evicting anything: a failed allocation
         (reachable from a remote GROUP frame via espnow_recv_cb) must leave the
         list exactly as it was, and must not be dereferenced below (CWE-476). */
    espnow_group_item_t *new_item = ESP_MALLOC(sizeof(espnow_group_item_t));
    if (!new_item) {
        ESP_LOGE(TAG, "ESP_MALLOC group item failed");
        return ESP_ERR_NO_MEM;
    }
    memcpy(new_item->id, group_id, 6);

    /**< Evict the oldest application group (the tail) rather than reject, so a
         one-time flood cannot block later joins. */
    if (group_count >= ESPNOW_GROUP_COUNT_MAX && !espnow_group_is_builtin(group_id)) {
        espnow_group_item_t *victim = NULL;

        SLIST_FOREACH(item, &g_group_item_list, next) {
            if (!espnow_group_is_builtin(item->id)) {
                victim = item;
            }
        }

        if (victim) {
            ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "Group list full (%u), evict oldest group_id " MACSTR,
                                    (unsigned)group_count, MAC2STR(victim->id));
            SLIST_REMOVE(&g_group_item_list, victim, espnow_group_item, next);
            ESP_FREE(victim);
        }
    }

    /**< Always insert at the head so the list is ordered newest to oldest and
         the tail, which eviction removes, is the oldest application group. */
    SLIST_INSERT_HEAD(&g_group_item_list, new_item, next);

    return ESP_OK;
}

esp_err_t espnow_del_group(const espnow_group_t group_id)
{
    espnow_group_item_t *item = NULL;

    SLIST_FOREACH(item, &g_group_item_list, next) {
        if (!memcmp(item->id, group_id, 6)) {
            SLIST_REMOVE(&g_group_item_list, item, espnow_group_item, next);
            ESP_FREE(item);
            return ESP_OK;
        }
    }

    return ESP_OK;
}

int espnow_get_group_num(void)
{
    int count = 0;
    espnow_group_item_t *item = NULL;

    SLIST_FOREACH(item, &g_group_item_list, next) {
        count++;
    }

    return count;
}

esp_err_t espnow_get_group_list(espnow_group_t group_id_list[], size_t num)
{
    espnow_group_item_t *item = NULL;
    int i = 0;

    SLIST_FOREACH(item, &g_group_item_list, next) {
        memcpy(group_id_list[i], item->id, 6);

        if (++i >= num) {
            return ESP_OK;
        }
    }

    return ESP_OK;
}

bool espnow_is_my_group(const espnow_group_t group_id)
{
    espnow_group_item_t *item = NULL;
    SLIST_FOREACH(item, &g_group_item_list, next) {
        if (!memcmp(item->id, group_id, 6)) {
            return true;
        }
    }

    return false;
}
