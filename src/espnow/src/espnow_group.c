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
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "espnow.h"
#include "esp_crc.h"

static const char *TAG = "espnow_group";

typedef struct espnow_group_item {
    espnow_group_t id;
    SLIST_ENTRY(espnow_group_item) next;
} espnow_group_item_t;

static SLIST_HEAD(group_item_list_, espnow_group_item) g_group_item_list;

esp_err_t espnow_add_group(const espnow_group_t group_id)
{
    espnow_group_item_t *item = NULL;

    SLIST_FOREACH(item, &g_group_item_list, next) {
        if (!memcmp(item->id, group_id, 6)) {
            return ESP_OK;
        }
    }

    espnow_group_item_t *list_first = SLIST_FIRST(&g_group_item_list);
    item = ESP_MALLOC(sizeof(espnow_group_item_t));
    memcpy(item->id, group_id, 6);

    if (!list_first) {
        SLIST_INSERT_HEAD(&g_group_item_list, item, next);
    } else {
        SLIST_INSERT_AFTER(list_first, item, next);
    }

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
