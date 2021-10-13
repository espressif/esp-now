// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
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

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <esp_log.h>

#include "esp_mem.h"

typedef struct {
    void *ptr;
    int size;
    const char *tag;
    int line;
    uint32_t timestamp;
} esp_mem_info_t;

static const char *TAG            = "esp_mem";
static uint32_t g_mem_count       = 0;
static esp_mem_info_t *g_mem_info = NULL;
static SemaphoreHandle_t g_mem_info_lock = NULL;

void esp_mem_add_record(void *ptr, int size, const char *tag, int line)
{
    if (!ptr || !size || !tag) {
        return;
    }

    ESP_LOGV(TAG, "<%s : %d> Alloc ptr: %p, size: %d, heap free: %d", tag, line,
             ptr, (int)size, esp_get_free_heap_size());

    if (!g_mem_info) {
        g_mem_info = calloc(MEM_DBG_INFO_MAX, sizeof(esp_mem_info_t));
    }

    if (g_mem_count >= MEM_DBG_INFO_MAX) {
        ESP_LOGE(TAG, "The buffer space of the memory record is full");
        esp_mem_print_record();
        return ;
    }

    if (!g_mem_info_lock) {
        g_mem_info_lock = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(g_mem_info_lock, portMAX_DELAY);

    for (int i = 0; i < MEM_DBG_INFO_MAX; i++) {
        if (!g_mem_info[i].size) {
            g_mem_info[i].ptr  = ptr;
            g_mem_info[i].tag  = tag;
            g_mem_info[i].line = line;
            g_mem_info[i].timestamp = esp_log_timestamp();
            g_mem_info[i].size = size;
            g_mem_count++;
            break;
        }
    }

    xSemaphoreGive(g_mem_info_lock);
}

void esp_mem_remove_record(void *ptr, const char *tag, int line)
{
    if (!ptr) {
        return;
    }

    ESP_LOGV(TAG, "<%s : %d> Free ptr: %p, heap free: %d", tag, line, ptr, esp_get_free_heap_size());

    if (!g_mem_info) {
        g_mem_info = calloc(MEM_DBG_INFO_MAX, sizeof(esp_mem_info_t));
    }

    if (!g_mem_info_lock) {
        g_mem_info_lock = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(g_mem_info_lock, portMAX_DELAY);

    for (int i = 0; i < MEM_DBG_INFO_MAX; i++) {
        if (g_mem_info[i].size && g_mem_info[i].ptr == ptr) {
            g_mem_info[i].size = 0;
            g_mem_count--;
            break;
        }
    }

    xSemaphoreGive(g_mem_info_lock);
}

void esp_mem_print_record(void)
{
    size_t total_size = 0;

    if (!ESP_MEM_DEBUG) {
        ESP_LOGW(TAG, "Please enable memory record");
    }

    if (!g_mem_count || !g_mem_info) {
        ESP_LOGW(TAG, "Memory record is empty");
        return ;
    }

    for (int i = 0; i < MEM_DBG_INFO_MAX; i++) {
        if (g_mem_info[i].size) {
            ESP_LOGI(TAG, "(%d) <%s: %d> ptr: %p, size: %d", g_mem_info[i].timestamp, g_mem_info[i].tag, g_mem_info[i].line,
                     g_mem_info[i].ptr, g_mem_info[i].size);
            total_size += g_mem_info[i].size;
        }
    }

    ESP_LOGI(TAG, "Memory record, num: %d, size: %d", g_mem_count, total_size);
}

void esp_mem_print_task()
{
#if ( ( configUSE_TRACE_FACILITY == 1 ) && ( configUSE_STATS_FORMATTING_FUNCTIONS > 0 ) )
    TaskStatus_t *pxTaskStatusArray = NULL;
    volatile UBaseType_t uxArraySize = 0;
    uint32_t ulTotalRunTime = 0, ulStatsAsPercentage = 0, ulRunTimeCounte = 0;
    const char task_status_char[] = {'r', 'R', 'B', 'S', 'D'};

    /* Take a snapshot of the number of tasks in case it changes while this
    function is executing. */
    uxArraySize = uxTaskGetNumberOfTasks();
    pxTaskStatusArray = malloc(uxTaskGetNumberOfTasks() * sizeof(TaskStatus_t));

    if (!pxTaskStatusArray) {
        return ;
    }

    /* Generate the (binary) data. */
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
    ulTotalRunTime /= 100UL;

    ESP_LOGI(TAG, "---------------- The State Of Tasks ----------------");
    ESP_LOGI(TAG, "- HWM   : usage high water mark (Byte)");
    ESP_LOGI(TAG, "- Status: blocked ('B'), ready ('R'), deleted ('D') or suspended ('S')");
    ESP_LOGI(TAG, "TaskName\t\tStatus\tPrio\tHWM\tTaskNum\tCoreID\tRunTimeCounter\tPercentage");

    for (int i = 0; i < uxArraySize; i++) {
#if( configGENERATE_RUN_TIME_STATS == 1 )
        ulRunTimeCounte = pxTaskStatusArray[i].ulRunTimeCounter;
        ulStatsAsPercentage = ulRunTimeCounte / ulTotalRunTime;
#else
#warning configGENERATE_RUN_TIME_STATS must also be set to 1 in FreeRTOSConfig.h to use vTaskGetRunTimeStats().
#endif

        int core_id = -1;
        char precentage_char[4] = {0};

#if ( configTASKLIST_INCLUDE_COREID == 1 )
        core_id = (int) pxTaskStatusArray[ i ].xCoreID;
#else
#warning configTASKLIST_INCLUDE_COREID must also be set to 1 in FreeRTOSConfig.h to use xCoreID.
#endif

        /* Write the rest of the string. */
        ESP_LOGI(TAG, "%-16s\t%c\t%u\t%u\t%u\t%hd\t%-16u%-s%%",
                 pxTaskStatusArray[i].pcTaskName, task_status_char[pxTaskStatusArray[i].eCurrentState],
                 (uint32_t) pxTaskStatusArray[i].uxCurrentPriority,
                 (uint32_t) pxTaskStatusArray[i].usStackHighWaterMark,
                 (uint32_t) pxTaskStatusArray[i].xTaskNumber, core_id,
                 ulRunTimeCounte, (ulStatsAsPercentage <= 0) ? "<1" : itoa(ulStatsAsPercentage, precentage_char, 10));
    }

    free(pxTaskStatusArray);
#endif /**< ( configUSE_TRACE_FACILITY == 1 ) && ( configUSE_STATS_FORMATTING_FUNCTIONS > 0 */
}


void esp_mem_print_heap(void)
{
#ifndef CONFIG_SPIRAM_SUPPORT
    ESP_LOGI(TAG, "Free heap, current: %d, minimum: %d",
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
#else
    ESP_LOGI(TAG, "Free heap, internal current: %d, minimum: %d, total current: %d, minimum: %d",
             heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
             esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
#endif /**< CONFIG_SPIRAM_SUPPORT */
}
