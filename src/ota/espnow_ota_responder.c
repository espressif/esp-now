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

#include <string.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "nvs.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#include "esp_efuse.h"
#endif

#include "espnow.h"
#include "espnow_ota.h"
#include "espnow_utils.h"

#define ESPNOW_OTA_STORE_CONFIG_KEY "upugrad_config"
#define CONFIG_ESPNOW_OTA_SKIP_VERSION_CHECK

/**< Usable capacity (in bytes) of status.progress_array, matching the g_ota_config
     allocation of `sizeof(ota_config_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE * ESPNOW_OTA_PROGRESS_CHUNK_NUM`.
     The attacker-controlled packet_num is bounded against this to avoid a heap overflow. */
#define ESPNOW_OTA_PROGRESS_ARRAY_CAPACITY (ESPNOW_OTA_PROGRESS_MAX_SIZE * ESPNOW_OTA_PROGRESS_CHUNK_NUM)
#define ESPNOW_OTA_BITMAP_PACKET_LIMIT     (((ESPNOW_OTA_PROGRESS_ARRAY_CAPACITY) - 1) * 8)

/**< Minimum spacing between peer-triggered OTA (re)starts; each esp_ota_begin
     erases the update partition, so a flood would cause flash wear and DoS.
     Fallback applies only when the Kconfig value is not defined (0 disables). */
#ifndef CONFIG_ESPNOW_OTA_RESTART_MIN_INTERVAL_MS
#define CONFIG_ESPNOW_OTA_RESTART_MIN_INTERVAL_MS 1000
#endif
#define ESPNOW_OTA_RESTART_MIN_INTERVAL_MS CONFIG_ESPNOW_OTA_RESTART_MIN_INTERVAL_MS

/**< packet_num is a 16-bit wire field; the configured firmware must not need more packets */
_Static_assert(ESPNOW_OTA_PACKET_MAX_NUM <= 65535,
               "CONFIG_ESPNOW_OTA_FIRMWARE_SIZE_MAX too large: OTA packet_num (uint16_t) would overflow");
typedef struct {
    esp_ota_handle_t handle;      /**< OTA handle */
    const esp_partition_t *partition; /**< Pointer to partition structure obtained using
                                           esp_partition_find_first or esp_partition_get */
    uint32_t start_time;         /**< Start time of the upgrade */
    espnow_ota_status_t status;  /**< Upgrade status */
} ota_config_t;

static const char *TAG = "espnow_ota_responder";
static ota_config_t *g_ota_config = NULL;
static bool g_ota_finished_flag        = false;
static uint32_t g_ota_last_begin_time  = 0;  /**< tick of last esp_ota_begin, for restart rate-limiting */
static espnow_frame_head_t g_frame_config = { .security = CONFIG_ESPNOW_OTA_SECURITY,
                                              .retransmit_count = CONFIG_ESPNOW_OTA_RETRANSMISSION_TIMES};
static espnow_ota_config_t *g_espnow_ota_config = NULL;

static esp_err_t validate_image_header(const esp_partition_t *update)
{
    esp_app_desc_t new_app_info;
    esp_app_desc_t running_app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

    if (esp_ota_get_partition_description(update, &new_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", new_app_info.version);
    }

    if (!g_espnow_ota_config->skip_version_check
            && !memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version))) {
        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
        return ESP_FAIL;
    }

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /**
     * Secure version check from firmware image header prevents subsequent download and flash write of
     * entire firmware image. However this is optional because it is also taken care in API
     * esp_https_ota_finish at the end of OTA update procedure.
     */
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();

    if (new_app_info.secure_version < hw_sec_version) {
        ESP_LOGW(TAG, "New firmware security version is less than eFuse programmed, %d < %d", new_app_info.secure_version, hw_sec_version);
        return ESP_FAIL;
    }

#endif

    return ESP_OK;
}

static esp_err_t espnow_ota_info(const uint8_t *src_addr)
{
    esp_err_t ret = ESP_OK;
    /**< Remove useless data, sha256 of elf file (32 Byte) +  reserv2 (20 Byte)*/
    size_t size = sizeof(espnow_ota_info_t) - 32 - 20;
    espnow_ota_info_t *info = ESP_MALLOC(sizeof(espnow_ota_info_t));

    /* ESP_MALLOC returns NULL on failure; guard before the deref below,
     * otherwise a pre-auth OTA REQUEST under heap pressure crashes the device. */
    ESP_ERROR_RETURN(!info, ESP_ERR_NO_MEM, "espnow_ota_info malloc");

    info->type = ESPNOW_OTA_TYPE_INFO;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    memcpy(&info->app_desc, esp_app_get_description(), sizeof(esp_app_desc_t));
#else
    memcpy(&info->app_desc, esp_ota_get_app_description(), sizeof(esp_app_desc_t));
#endif

    ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, info, size, &g_frame_config, portMAX_DELAY);

    if (ret != ESP_OK) {
        /* Free before the early return; otherwise a failed send (e.g. a forged
         * requester that never ACKs) leaks the info buffer on every attempt. */
        ESP_FREE(info);
    }
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    ESP_LOGD(TAG, "Application information:");
    ESP_LOGD(TAG, "Project name:     %s", info->app_desc.project_name);
    ESP_LOGD(TAG, "App version:      %s", info->app_desc.version);
    ESP_LOGD(TAG, "Secure version:   %d", info->app_desc.secure_version);
    ESP_LOGD(TAG, "Compile time:     %s %s", info->app_desc.date, info->app_desc.time);
    ESP_LOGD(TAG, "ESP-IDF:          %s", info->app_desc.idf_ver);

    ESP_FREE(info);

    return ESP_OK;
}

static esp_err_t espnow_ota_status_handle(const espnow_addr_t src_addr, const espnow_ota_status_t *status, size_t size)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(status);
    ESP_PARAM_CHECK(size);

    /**< The checks below reject frame contents any device in range can send
         (OTA frames are plaintext unless CONFIG_ESPNOW_OTA_SECURITY), so their
         warnings are rate-limited per call site; the error code is unchanged. */

    /**< Reject a truncated frame so status->packet_num is never read out of bounds */
    if (size < sizeof(espnow_ota_status_t)) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA STATUS frame too short: %u < %u",
                                (unsigned)size, (unsigned)sizeof(espnow_ota_status_t));
        return ESP_ERR_INVALID_ARG;
    }

    /**< Bound the attacker-controlled packet_num before it is used as a memset/storage
         length over status.progress_array, otherwise packet_num up to 0xFFFF yields an
         8 KB write into a ~2 KB region (remote unauthenticated heap overflow). */
    if (status->packet_num > ESPNOW_OTA_BITMAP_PACKET_LIMIT) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA packet_num %u exceeds bitmap capacity (max %u)",
                                status->packet_num, (unsigned)ESPNOW_OTA_BITMAP_PACKET_LIMIT);
        return ESP_ERR_INVALID_ARG;
    }

    /**< Reject total_size == 0: esp_ota_begin(0) succeeds as OTA_SIZE_UNKNOWN,
         arming the DATA path for a later divide-by-zero in the progress calc.
         A real firmware image always has a non-zero size. */
    if (status->total_size == 0) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA STATUS frame with zero total_size");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret        = ESP_ERR_NO_MEM;
    size_t response_size = sizeof(espnow_ota_status_t);
    /**< The running partition hash is constant; compute it once, not per STATUS frame. */
    static uint8_t s_running_sha_256[32] = {0};
    static bool s_running_sha_256_valid  = false;

    if (!g_ota_config) {
        size_t config_size = sizeof(ota_config_t) + ESPNOW_OTA_PROGRESS_ARRAY_CAPACITY;
        g_ota_config   = ESP_CALLOC(1, config_size);
        /**< EXIT dereferences g_ota_config */
        ESP_ERROR_RETURN(!g_ota_config, ESP_ERR_NO_MEM, "<ESP_ERR_NO_MEM> g_ota_config");

        /**< Pass the buffer capacity so nvs_get_blob rejects an oversized blob
             instead of overflowing g_ota_config. */
        ret = espnow_storage_get(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config, config_size);

        /**< Discard a persisted session that does not fit or would overflow the bitmap. */
        if (ret != ESP_OK || g_ota_config->status.packet_num > ESPNOW_OTA_BITMAP_PACKET_LIMIT
                || g_ota_config->status.total_size == 0) {
            memset(g_ota_config, 0, config_size);
            if (ret != ESP_ERR_NVS_NOT_FOUND) {
                espnow_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);
            }
        }

        /**< The persisted handle belongs to the previous boot. */
        g_ota_config->handle = 0;

        g_ota_config->start_time = xTaskGetTickCount();
        g_ota_config->partition = esp_ota_get_next_update_partition(NULL);
    }

    /**< Every reply below is sent from g_ota_config->status. On a device that has
         not run a session yet the struct is still zeroed, and the initiator drops a
         STATUS reply whose type is not ESPNOW_OTA_TYPE_STATUS: the "same as running
         firmware" answer would then never be counted as finished. The type is a
         constant, so setting it does not touch the session state. */
    g_ota_config->status.type = ESPNOW_OTA_TYPE_STATUS;

    /**< Do not touch the current session here: every rejection below must leave
         its error_code (e.g. ESP_ERR_ESPNOW_OTA_STOP), progress and handle intact,
         otherwise a rejected STATUS re-enables the DATA path of a stopped session. */

    if (!s_running_sha_256_valid) {
        ret = esp_partition_get_sha256(esp_ota_get_running_partition(), s_running_sha_256);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_partition_get_sha256");
        s_running_sha_256_valid = true;
    }

    if (!memcmp(s_running_sha_256, status->sha_256, ESPNOW_OTA_HASH_LEN)) {
        /**< Reachable by replay; rate-limit */
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "The firmware to be upgraded is the same as the currently running firmware, so no upgrade");
        /**< Answer this sender from a local copy of its own request. The request
             names the running image, so it is not the session in g_ota_config (if
             any): writing FINISH / written_size 0 into g_ota_config would mark an
             unrelated download of another image as finished (espnow_ota_write()
             then drops its DATA and the next STATUS for it takes the finished
             shortcut) or clear ESP_ERR_ESPNOW_OTA_STOP. The initiator only looks
             at error_code / written_size, so the reply needs nothing else. */
        espnow_ota_status_t reply;
        memcpy(&reply, status, sizeof(espnow_ota_status_t));
        reply.type           = ESPNOW_OTA_TYPE_STATUS;
        reply.error_code     = ESP_ERR_ESPNOW_OTA_FINISH;
        reply.written_size   = 0;
        reply.progress_index = 0;
        ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, &reply,
                          sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");
        return ESP_OK;
    }

    /**< A STATUS for the image of the current session only reports the real session
         state (progress, or ESP_ERR_ESPNOW_OTA_FINISH) while that session is in
         progress or finished. A stopped or failed session is not resumed silently:
         it goes through the full validation path below and, if accepted, is restarted
         with a fresh esp_ota_begin() handle. */
    if (!memcmp(g_ota_config->status.sha_256, status->sha_256, ESPNOW_OTA_HASH_LEN)
            && g_ota_config->status.total_size == status->total_size
            && (g_ota_config->status.error_code == ESP_OK
                || g_ota_config->status.error_code == ESP_ERR_ESPNOW_OTA_FINISH)) {
        ret = ESP_OK;
        goto EXIT;
    }

    /**< Validate the new STATUS fully before touching the current session: the
         rate limit and the checks below return without modifying g_ota_config. */

    /**< Rate-limit restarts: each esp_ota_begin erases the update partition. */
    uint32_t now = xTaskGetTickCount();
    if (g_ota_last_begin_time != 0
            && (now - g_ota_last_begin_time) < pdMS_TO_TICKS(ESPNOW_OTA_RESTART_MIN_INTERVAL_MS)) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA restart rate-limited (min interval %d ms), keeping the current session",
                                ESPNOW_OTA_RESTART_MIN_INTERVAL_MS);
        return ESP_ERR_INVALID_STATE;
    }

    /**< Get partition info of currently running app
    Return the next OTA app partition which should be written with a new firmware.*/
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update  = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_RETURN(!running || !update, ESP_ERR_ESPNOW_OTA_FIRMWARE_PARTITION,
                     "No partition is found or flash read operation failed");
    ESP_LOGD(TAG, "Running partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             running->label, running->type, running->subtype, running->address);
    ESP_LOGD(TAG, "Update partition, label: %s, type: 0x%x, subtype: 0x%x, address: 0x%x",
             update->label, update->type, update->subtype, update->address);

    /**< total_size and packet_num come from the peer: total_size must fit the
         partition (0xFFFFFFFF means OTA_SIZE_UNKNOWN) and packet_num must match it. */
    if (status->total_size > update->size) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA total_size %u exceeds update partition size %u",
                                (unsigned)status->total_size, (unsigned)update->size);
        return ESP_ERR_INVALID_ARG;
    }
    if (status->packet_num != (status->total_size + ESPNOW_OTA_PACKET_MAX_SIZE - 1) / ESPNOW_OTA_PACKET_MAX_SIZE) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA packet_num %u does not match total_size %u",
                                status->packet_num, (unsigned)status->total_size);
        return ESP_ERR_INVALID_ARG;
    }

    /**< Release the replaced session's handle, otherwise esp_ota_begin leaks. */
    if (g_ota_config->handle) {
        esp_ota_abort(g_ota_config->handle);
        g_ota_config->handle = 0;
    }

    memset(g_ota_config, 0, sizeof(ota_config_t));
    memcpy(&g_ota_config->status, status, sizeof(espnow_ota_status_t));
    memset(g_ota_config->status.progress_array, 0, status->packet_num / 8 + 1);
    g_ota_config->status.written_size = 0;
    g_ota_config->status.error_code = ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT;
    g_ota_config->partition  = update;
    g_ota_config->start_time = xTaskGetTickCount();

    /**< Tell the initiator to wait while the partition is erased. If this send fails
         the session stays in ESP_ERR_ESPNOW_OTA_FIRMWARE_NOT_INIT: espnow_ota_write()
         ignores DATA for it and the next STATUS for the same image goes through the
         full path above (the same-image shortcut requires ESP_OK / FINISH) and runs
         esp_ota_begin(). The restart timestamp is only taken when esp_ota_begin() is
         actually attempted, so that retry is not rate-limited. */
    ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                      sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    ESP_LOGI(TAG, "The device starts to upgrade");

    g_ota_finished_flag = false;
    g_ota_last_begin_time = now;

    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(update, g_ota_config->status.total_size, &g_ota_config->handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "esp_ota_begin failed");

    /**< Save upgrade information to flash. */
    ret = espnow_storage_set(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config,
                          sizeof(ota_config_t) + g_ota_config->status.packet_num / 8 + 1);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "info_store_save, ret: %d", ret);

    /**< Send ESP_EVENT_ESPNOW_OTA_STARTED event to the event handler */
    esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_OTA_STARTED, NULL, 0, 0);

    g_ota_config->status.error_code = ESP_OK;

EXIT:

    /**< Update g_ota_config->status */
    if (g_ota_config->status.written_size
            && g_ota_config->status.written_size != g_ota_config->status.total_size) {
        espnow_ota_status_t *tmp_status = ESP_MALLOC(sizeof(espnow_ota_status_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE);
        /**< Reached from a remote STATUS frame; a failed allocation under memory
             pressure must not be dereferenced by the memcpy below (CWE-476). */
        ESP_ERROR_RETURN(!tmp_status, ESP_ERR_NO_MEM, "ESP_MALLOC tmp_status");
        memcpy(tmp_status, &g_ota_config->status, sizeof(espnow_ota_status_t));
        espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
        frame_head.security = CONFIG_ESPNOW_OTA_SECURITY;

        for (int seq = 0; seq < tmp_status->packet_num; ++seq) {
            if (!ESPNOW_OTA_GET_BITS(g_ota_config->status.progress_array, seq)) {
                tmp_status->progress_index = seq / (ESPNOW_OTA_PROGRESS_MAX_SIZE * 8);
                memcpy(tmp_status->progress_array[0], g_ota_config->status.progress_array[tmp_status->progress_index], ESPNOW_OTA_PROGRESS_MAX_SIZE);
                ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, tmp_status,
                                  sizeof(espnow_ota_status_t) + ESPNOW_OTA_PROGRESS_MAX_SIZE, &frame_head, portMAX_DELAY);
                ESP_LOG_BUFFER_HEXDUMP(TAG, tmp_status->progress_array, ESPNOW_OTA_PROGRESS_MAX_SIZE, ESP_LOG_DEBUG);
                ESP_ERROR_BREAK(ret != ESP_OK, "espnow_send");

                ESP_LOGD(TAG, "status, total_size: %d, written_size: %d, progress_index: %d",
                         tmp_status->total_size, tmp_status->written_size, tmp_status->progress_index);
                break;
            }
        }

        ESP_FREE(tmp_status);
        return ret;
    }

    if (ret != ESP_OK) {
        g_ota_config->status.error_code = ret;
    }

    ESP_LOGD(TAG, "Response upgrade status, written_size: %d, response_size: %d, addr: " MACSTR,
             g_ota_config->status.written_size, response_size, MAC2STR(src_addr));
    ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                      sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    return ESP_OK;
}

static esp_err_t espnow_ota_write(const espnow_addr_t src_addr, const espnow_ota_packet_t *packet, size_t size)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(packet);
    ESP_PARAM_CHECK(size);

    esp_err_t ret = ESP_OK;

    /**< The frame is cast to espnow_ota_packet_t and packet->seq / packet->size
         (offsets 1..3) are dereferenced below before the payload-length guard.
         Reject a frame shorter than the fixed header first, otherwise a truncated
         DATA frame over-reads the RX buffer (CWE-125). The header length is
         sizeof(espnow_ota_packet_t) minus the trailing data[] array.
         As in espnow_ota_status_handle, the warnings for rejected frame contents
         are rate-limited per call site. */
    if (size < sizeof(espnow_ota_packet_t) - ESPNOW_OTA_PACKET_MAX_SIZE) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA DATA frame too short for header: %u", (unsigned)size);
        return ESP_ERR_INVALID_ARG;
    }

    /* Return if not recv status or firmware has been upgraded */
    if (!g_ota_config || g_ota_config->status.error_code == ESP_ERR_ESPNOW_OTA_FINISH) {
        return ESP_OK;
    }

    if (g_ota_config->status.error_code == ESP_ERR_ESPNOW_OTA_STOP) {
        g_ota_config->status.type         = ESPNOW_OTA_TYPE_STATUS;
        g_ota_config->status.written_size = 0;
        memset(g_ota_config->status.progress_array, 0, g_ota_config->status.packet_num / 8 + 1);
        espnow_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);

        ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                          sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

        return ESP_OK;
    }

    /**< Only a session whose esp_ota_begin() completed accepts DATA. error_code is
         ESP_OK once the session started (also in the persisted copy used to resume
         after a reboot, where the handle is 0 by design); a session whose first STATUS
         reply failed (FIRMWARE_NOT_INIT) or whose esp_ota_begin() / storage step failed
         has no erased partition behind it and is restarted by the next STATUS. */
    if (g_ota_config->status.error_code != ESP_OK) {
        ESP_LOGD(TAG, "OTA session not started (error_code 0x%x), DATA ignored", g_ota_config->status.error_code);
        return ESP_OK;
    }

    /**< Bound the attacker-controlled packet->seq before it indexes status.progress_array
         (ESPNOW_OTA_GET_BITS/SET_BITS below), so a large seq cannot read/write outside the
         bitmap regardless of total_size or the configured firmware size. */
    if (packet->seq > ESPNOW_OTA_BITMAP_PACKET_LIMIT) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA packet seq %u exceeds bitmap capacity (max %u)",
                                packet->seq, (unsigned)ESPNOW_OTA_BITMAP_PACKET_LIMIT);
        return ESP_ERR_INVALID_ARG;
    }

    /* Reject seq >= packet_num: seq == packet_num passes a ">total_size" test when
     * total_size is a multiple of the packet size, writing past the declared image
     * end and pushing written_size beyond total_size so the upgrade never completes. */
    if (packet->seq >= g_ota_config->status.packet_num) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA packet seq %u exceeds packet count %u",
                                packet->seq, g_ota_config->status.packet_num);
        return ESP_ERR_INVALID_ARG;
    }

    /**< Received a duplicate packet */
    if (ESPNOW_OTA_GET_BITS(g_ota_config->status.progress_array, packet->seq)) {
        ESP_LOGD(TAG, "Received a duplicate packet, packet_seq: %d", packet->seq);
        return ESP_OK;
    }

    /**< packet->size is attacker-controlled and used as the esp_partition_write
         source length. Reject a size beyond the per-packet capacity or the bytes
         actually received, otherwise the write over-reads the RX buffer into flash.
         It must also be exactly the length this seq carries on the wire: the
         initiator sends ESPNOW_OTA_PACKET_MAX_SIZE for every packet but the last,
         which carries the remainder (packet_num was checked against total_size when
         the STATUS was accepted, so the remainder is in 1..MAX). Any other length
         would still set the progress bit and be added to written_size, so the
         session could never reach written_size == total_size and the correct
         retransmission would then be dropped as a duplicate. */
    uint32_t expect_size = ESPNOW_OTA_PACKET_MAX_SIZE;
    if (packet->seq == g_ota_config->status.packet_num - 1) {
        expect_size = g_ota_config->status.total_size - (uint32_t)packet->seq * ESPNOW_OTA_PACKET_MAX_SIZE;
    }
    if (packet->size > ESPNOW_OTA_PACKET_MAX_SIZE || packet->size != expect_size
            || size < (sizeof(espnow_ota_packet_t) - ESPNOW_OTA_PACKET_MAX_SIZE) + packet->size) {
        ESPNOW_LOG_RATE_LIMITED(ESP_LOGW, 1000, "OTA DATA seq %u size %u, expected %u (frame len %u)",
                                packet->seq, (unsigned)packet->size, (unsigned)expect_size, (unsigned)size);
        return ESP_ERR_INVALID_ARG;
    }

    /**< Write firmware data to the update partition */
    ret = esp_partition_write(g_ota_config->partition, packet->seq * ESPNOW_OTA_PACKET_MAX_SIZE,
                              packet->data, packet->size);
    ESP_ERROR_RETURN(ret != ESP_OK, ESP_ERR_ESPNOW_OTA_FIRMWARE_DOWNLOAD,
                     "esp_partition_write %s", esp_err_to_name(ret));

    ESPNOW_OTA_SET_BITS(g_ota_config->status.progress_array, packet->seq);
    g_ota_config->status.written_size += packet->size;

    /**< Save OTA status periodically, it can be used to
         resumable data transfers from breakpoint after system reset */
    static uint32_t s_next_written_percentage = 0;
    /**< Never divide by total_size unless it is non-zero. */
    if (g_espnow_ota_config->progress_report_interval && g_ota_config->status.total_size) {
        uint32_t written_percentage = g_ota_config->status.written_size * 100 / g_ota_config->status.total_size;

        if (!s_next_written_percentage) {
            s_next_written_percentage = g_espnow_ota_config->progress_report_interval;
        }

        ESP_LOGD(TAG, "packet_seq: %d, packet_size: %d, written_size: %d, progress: %03d%%, next_percentage: %03d%%",
                 packet->seq, packet->size, g_ota_config->status.written_size, written_percentage, s_next_written_percentage);

        if (written_percentage == s_next_written_percentage) {
            ESP_LOGD(TAG, "Save the data of upgrade status to flash");
            s_next_written_percentage += g_espnow_ota_config->progress_report_interval;

            espnow_storage_set(ESPNOW_OTA_STORE_CONFIG_KEY, g_ota_config,
                            sizeof(ota_config_t) + g_ota_config->status.packet_num / 8 + 1);

            /**< Send ESP_EVENT_ESPNOW_OTA_STATUS event to the event handler */
            esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_OTA_STATUS, &written_percentage, sizeof(uint32_t), 0);

            ESP_LOGD(TAG, "packet_seq: %d, packet_size: %d, written_size: %d, progress: %d%%",
                     packet->seq, packet->size, g_ota_config->status.written_size, written_percentage);
        } else if (written_percentage > s_next_written_percentage) {
            s_next_written_percentage = (written_percentage / g_espnow_ota_config->progress_report_interval + 1) * g_espnow_ota_config->progress_report_interval;
        }
    }

    if (g_ota_config->status.written_size == g_ota_config->status.total_size) {
        s_next_written_percentage = 0;
        ESP_LOG_BUFFER_CHAR_LEVEL(TAG, g_ota_config->status.progress_array,
                                  ESPNOW_OTA_PROGRESS_MAX_SIZE, ESP_LOG_VERBOSE);
        ESP_LOGI(TAG, "Write total_size: %d, written_size: %d, spend time: %ds",
                 g_ota_config->status.total_size, g_ota_config->status.written_size,
                 (xTaskGetTickCount() - g_ota_config->start_time) * portTICK_PERIOD_MS / 1000);

        /**< If ESP32 was reset duration OTA, and after restart, the update_handle will be invalid,
             but it still can switch boot partition and reboot successful */
        esp_ota_end(g_ota_config->handle);
        espnow_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);

        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

        ret = validate_image_header(update_partition);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "validate_image_header");

        ret = esp_ota_set_boot_partition(update_partition);

        if (ret != ESP_OK) {
            g_ota_config->status.written_size = 0;
            g_ota_config->status.error_code   = ESP_ERR_ESPNOW_OTA_STOP;
            ESP_LOGW(TAG, "<%s> esp_ota_set_boot_partition", esp_err_to_name(ret));
            return ret;
        }

        /**< Send ESP_EVENT_ESPNOW_OTA_FINISH event to the event handler */
        g_ota_finished_flag = true;
        g_ota_config->status.type = ESPNOW_OTA_TYPE_STATUS;

        /**< Response firmware upgrade status to root node. */
        ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, src_addr, &g_ota_config->status,
                          sizeof(espnow_ota_status_t), &g_frame_config, portMAX_DELAY);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_send");
    
        esp_event_post(ESP_EVENT_ESPNOW, ESP_EVENT_ESPNOW_OTA_FINISH, NULL, 0, 0);
    }

    return ESP_OK;
}

esp_err_t espnow_ota_responder_get_status(espnow_ota_status_t *status)
{
    ESP_PARAM_CHECK(status);
    ESP_ERROR_RETURN(!g_ota_config, ESP_ERR_NOT_SUPPORTED, "Mupgrade firmware is not initialized");

    memcpy(status, &g_ota_config->status, sizeof(espnow_ota_status_t));

    return ESP_OK;
}

esp_err_t espnow_ota_responder_stop()
{
    esp_err_t ret = ESP_OK;

    if (!g_ota_config) {
        return ESP_OK;
    }

    if (g_ota_finished_flag) {
        const esp_partition_t *running = esp_ota_get_running_partition();
        ret = esp_ota_set_boot_partition(running);
        ESP_ERROR_RETURN(ret != ESP_OK, ret, "esp_ota_set_boot_partition");
    }

    if (g_ota_config->handle) {
        esp_ota_abort(g_ota_config->handle);
        g_ota_config->handle = 0;
    }

    g_ota_config->status.type         = ESPNOW_OTA_TYPE_STATUS;
    g_ota_config->status.error_code   = ESP_ERR_ESPNOW_OTA_STOP;
    g_ota_config->status.written_size = 0;
    memset(g_ota_config->status.progress_array, 0, g_ota_config->status.packet_num / 8 + 1);
    espnow_storage_erase(ESPNOW_OTA_STORE_CONFIG_KEY);
    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    frame_head.security = CONFIG_ESPNOW_OTA_SECURITY;

    ret = espnow_send(ESPNOW_DATA_TYPE_OTA_STATUS, ESPNOW_ADDR_BROADCAST,
                      &g_ota_config->status, sizeof(espnow_ota_status_t), &frame_head, portMAX_DELAY);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "espnow_write");

    return ESP_OK;
}

static esp_err_t espnow_ota_responder_data_process(uint8_t *src_addr, void *data,
                      size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    esp_err_t ret = ESP_OK;
    uint8_t data_type = ((uint8_t *)data)[0];
    /* Only remove the peer if this handler added it: src_addr is forgeable, so a
     * frame naming an existing long-lived peer must not delete it on cleanup. */
    bool peer_existed = esp_now_is_peer_exist(src_addr);
    espnow_add_peer(src_addr, NULL);

    switch (data_type) {
        case ESPNOW_OTA_TYPE_REQUEST:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_INFO");
            ret = espnow_ota_info(src_addr);
            break;

        case ESPNOW_OTA_TYPE_STATUS:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_STATUS");
            ret = espnow_ota_status_handle(src_addr, (espnow_ota_status_t *)data, size);
            break;

        case ESPNOW_OTA_TYPE_DATA:
            ESP_LOGD(TAG, "ESPNOW_OTA_TYPE_DATA");
            ret = espnow_ota_write(src_addr, (espnow_ota_packet_t *)data, size);
            break;

        default:
            break;
    }

    if (!peer_existed) {
        espnow_del_peer(src_addr);
    }
    /**< Every error path above has already logged (rate-limited where a frame can
         trigger it); logging here again would double every line. */
    return ret;
}

esp_err_t espnow_ota_responder_start(const espnow_ota_config_t *config)
{
    ESP_PARAM_CHECK(config);

    g_espnow_ota_config = ESP_MALLOC(sizeof(espnow_ota_config_t));
    ESP_ERROR_RETURN(!g_espnow_ota_config, ESP_ERR_NO_MEM, "ESP_MALLOC g_espnow_ota_config");
    memcpy(g_espnow_ota_config, config, sizeof(espnow_ota_config_t));
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_OTA_DATA, 1, espnow_ota_responder_data_process);

    return ESP_OK;
}
