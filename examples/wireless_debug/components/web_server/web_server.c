/* HTTP Restful API Server

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_log.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#include "esp_mac.h"
#endif

#include "esp_vfs.h"
#include "cJSON.h"

#include "esp_console.h"
#include "esp_console.h"
#include "espnow_ota.h"
#include "espnow.h"
#include "dns_server.h"


static const char *TAG = "rest_server";

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";

    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }

    return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    ESP_LOGD(TAG, "[%s, %d]: uri: %s", __func__, __LINE__, req->uri);

    char filepath[FILE_PATH_MAX];
    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;

    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    strlcat(filepath, req->uri, sizeof(filepath));

    int fd = open(filepath, O_RDONLY, 0);

    if (fd == -1) {
        ESP_LOGD(TAG, "[%s, %d] filepath: %s", __func__, __LINE__, filepath);

        const char *scratch = "index.html";
        sprintf(filepath, "%s/%s", rest_context->base_path, scratch);

        fd = open(filepath, O_RDONLY, 0);
    }

    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;

    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);

        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);

    /* Close file after sending complete */
    close(fd);

    ESP_LOGD(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/*
 * Structure holding server handle
 * and internal socket fd in order
 * to use out of request send
 */
typedef struct {
    httpd_handle_t hd;
    int fd;
} async_resp_t;

static async_resp_t *g_resp_handle = NULL;

/*
 * async send function, which we put into the httpd work queue
 */
esp_err_t web_server_send(const uint8_t *addr, const char *data,
                          size_t size, const wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    if (!g_resp_handle) {
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    char *log_data = NULL;
    size_t log_size = 0;
    char *buffer = ESP_CALLOC(1, size);

    uint32_t timestamp = 0;
    char log_level[2] = {0};
    char tag[32] = {0};

    ret = sscanf((char *)data + ((data[0] == '\033') ? 7 : 0), "%1s (%d) %[^:]: %[^\r\n\033]%*c",
                 log_level, &timestamp, tag, buffer);
    log_size = asprintf(&log_data, "{\"errno\":0,\"src_addr\":\""MACSTR"\",\"rssi\":%d,\"timetamp\":%u,\"log_level\":\"%s\",\"tag\":\"%s\",\"data\":\"%.*s\"}",
                        MAC2STR(addr), rx_ctrl->rssi, timestamp, log_level, tag, strlen(buffer), buffer);

    ESP_LOGD(TAG, "ret: %d, %s", ret, log_data);

    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)log_data,
        .len     = log_size,
        .type    = HTTPD_WS_TYPE_TEXT,
    };

    ret = httpd_ws_send_frame_async(g_resp_handle->hd, g_resp_handle->fd, &ws_pkt);

    ESP_FREE(log_data);
    ESP_FREE(buffer);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "<%s> httpd_ws_send_frame_async, err_str: %s", esp_err_to_name(ret), strerror(errno));
        close(g_resp_handle->fd);
        ESP_FREE(g_resp_handle);
        return ret;
    }

    return ESP_OK;
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t debug_recv_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    uint8_t buf[64] = { 0 };

    httpd_ws_frame_t ws_pkt = {
        .payload = buf,
        .type    = HTTPD_WS_TYPE_TEXT,
    };

    ret = httpd_ws_recv_frame(req, &ws_pkt, 64);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT &&
            strcmp((char *)ws_pkt.payload, "Trigger async") == 0) {
        if (!g_resp_handle) {
            g_resp_handle = ESP_MALLOC(sizeof(async_resp_t));
        }

        g_resp_handle->hd = req->handle;
        g_resp_handle->fd = httpd_req_to_sockfd(req);
    } else {
        ret = httpd_ws_send_frame(req, &ws_pkt);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
        }
    }

    return ret;
}

/* Simple handler for light brightness control */
static esp_err_t debug_send_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;

    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);

        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }

        cur_len += received;
    }

    buf[total_len] = '\0';

    ESP_LOGI(TAG, "command: %s", buf);

    esp_err_t err = esp_console_run(buf, &ret);
    char *resp_data = NULL;

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Unrecognized command");
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"Unrecognized command\"}", err);
    } else if (err == ESP_ERR_INVALID_ARG) {
        /**< Command was empty */
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"Command was empty\"}", err);
    } else if (err == ESP_OK && ret != ESP_OK) {
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"Command returned non-zero error code: %s\"}", ret, esp_err_to_name(ret));
    } else if (err != ESP_OK) {
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"Internal error: %s\"}", err, esp_err_to_name(err));
    } else {
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"success\"}", ESP_OK);
    }

    ESP_LOGI(TAG, "resp_data: %s", resp_data);

    httpd_resp_sendstr(req, resp_data);
    ESP_FREE(resp_data);

    return ESP_OK;
}


#define ESP_OTA_DATA_LEN      (1800 * 1024)
#define OTA_DATA_PAYLOAD_LEN  1460

static size_t g_ota_size = 0;

esp_err_t ota_initiator_data_cb(size_t src_offset, void* dst, size_t size)
{
    static const esp_partition_t *data_partition = NULL;

    if (!data_partition) {
        data_partition = esp_ota_get_next_update_partition(NULL);
    }

    return esp_partition_read(data_partition, src_offset, dst, size);
}

static void ota_send_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    size_t addrs_num = 0;
    espnow_ota_result_t ota_result = {0};
    uint32_t start_time = xTaskGetTickCount();
    uint8_t (* addrs_list)[ESPNOW_ADDR_LEN] = ESP_MALLOC(ESPNOW_ADDR_LEN);

    for (const char *tmp = (char *)arg;; tmp++) {
        if (*tmp == ',' || *tmp == ' ' || *tmp == '|' || *tmp == '.' || *tmp == '\0') {
            espnow_mac_str2hex(tmp - 17, addrs_list[addrs_num]);
            addrs_num++;

            if (*tmp == '\0' || *(tmp + 1) == '\0') {
                break;
            }

            addrs_list = ESP_REALLOC(addrs_list, ESPNOW_ADDR_LEN * (addrs_num + 1));
        }
    }

    uint8_t sha_256[32] = {0};
    const esp_partition_t *data_partition = esp_ota_get_next_update_partition(NULL);
    ret = esp_partition_get_sha256(data_partition, sha_256);
    ESP_ERROR_GOTO(ret!= ESP_OK, EXIT, "<%s> esp_partition_get_sha256", esp_err_to_name(ret));

    ret = espnow_ota_initiator_send(addrs_list, addrs_num, sha_256, g_ota_size,
                                    ota_initiator_data_cb, &ota_result);
    ESP_ERROR_GOTO(ret!= ESP_OK, EXIT, "<%s> esp_partition_get_sha256", esp_err_to_name(ret));

    ESP_LOGI(TAG, "Firmware is sent to the device to complete, Spend time: %ds",
                (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000);
    ESP_LOGI(TAG, "Devices upgrade completed, successed_num: %d, unfinished_num: %d",
                ota_result.successed_num, ota_result.unfinished_num);

    ESP_FREE(addrs_list);
    espnow_ota_initiator_result_free(&ota_result);

EXIT:
    ESP_FREE(arg);

    vTaskDelete(NULL);
}

static esp_err_t ota_data_handler(httpd_req_t *req)
{
    esp_err_t ret = ESP_OK;
    int total_size = req->content_len;
    char *resp_data = NULL;
    uint32_t start_time = xTaskGetTickCount();
    esp_ota_handle_t ota_handle = 0;
    uint8_t *data  = ESP_MALLOC(OTA_DATA_PAYLOAD_LEN);

    const esp_partition_t *updata_partition = esp_ota_get_next_update_partition(NULL);
    /**< Commence an OTA update writing to the specified partition. */
    ret = esp_ota_begin(updata_partition, total_size, &ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_begin failed, total_size", esp_err_to_name(ret));

    if (total_size >= ESP_OTA_DATA_LEN) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }

   for (ssize_t size = 0, recv_size = 0, i = 0; recv_size < total_size; ++i) {
        size = httpd_req_recv(req, (char *)data, OTA_DATA_PAYLOAD_LEN);

        if (size > 0) {
            /**< Write OTA update data to partition */
            ret = esp_ota_write(ota_handle, data, size);
            ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> Write firmware to flash, size: %d, data: %.*s",
                esp_err_to_name(ret), size, size, data);
        } else {
            ESP_LOGW(TAG, "<%s> esp_http_client_read, str_errno: %s", esp_err_to_name(ret), strerror(errno));
            ret = ESP_FAIL;
            goto EXIT;
        }

        recv_size += size;
    
        if (i % 100 == 0 || recv_size == total_size) {
            ESP_LOGI(TAG, "Firmware download size: %d, progress rate: %d%%",
                     recv_size, recv_size * 100 / total_size);
        }
    }

    ESP_LOGI(TAG, "The service download firmware is complete, total_size: %d, Spend time: %ds",
             total_size, pdTICKS_TO_MS(xTaskGetTickCount() - start_time) / 1000);

    ret = esp_ota_end(ota_handle);
    ESP_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_ota_end", esp_err_to_name(ret));

    size_t size = httpd_req_get_hdr_value_len(req, "Device-List") + 1;
    ESP_ERROR_GOTO(size <= 0, EXIT, "Search for %s in request headers", "Device-List");

    char *addrs_list = ESP_CALLOC(1, size + 1);

    if (httpd_req_get_hdr_value_str(req, "Device-List", addrs_list, size) != ESP_OK) {
        ESP_LOGD(TAG, "Get the value string of %s from the request headers", "Device-List");
        ESP_FREE(addrs_list);
        goto EXIT;
    }

    g_ota_size = total_size;

    xTaskCreate(ota_send_task, "ota_send", 4 * 1024, addrs_list, tskIDLE_PRIORITY + 1, NULL);

EXIT:    
    if (ret != ESP_OK) {
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"Internal error: %s\"}", ret, esp_err_to_name(ret));
    } else {
        asprintf(&resp_data, "{\"errno\":%d, \"err_msg\":\"success\"}", ESP_OK);
    }

    ESP_LOGI(TAG, "resp_data: %s", resp_data);

    httpd_resp_sendstr(req, resp_data);
    ESP_FREE(resp_data);
    ESP_FREE(data);

    return ESP_OK;
}

static const ip_addr_t ipaddr = IPADDR4_INIT_BYTES(192, 168, 4, 1);

/* handle any DNS requests from dns-server */
static bool dns_query_proc(const char *name, ip_addr_t *addr)
{
    /**
     * captive: enerate_204, cp.a, hotspot-detect.html
     */
#if CONFIG_LWIP_IPV6
    ESP_LOGD(TAG, "name: %s, ip_addr: " IPSTR, name, IP2STR(&addr->u_addr.ip4));
#else
    ESP_LOGD(TAG, "name: %s, ip_addr: " IPSTR, name, IP2STR(addr));
#endif

    *addr = ipaddr;
    return true;
}

esp_err_t web_server_start(const char *base_path)
{
    ESP_PARAM_CHECK(base_path);
    rest_server_context_t *rest_context = ESP_CALLOC(1, sizeof(rest_server_context_t));
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    esp_err_t ret = ESP_OK;
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.recv_wait_timeout  = 30;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    ret = httpd_start(&server, &config);
    ESP_ERROR_GOTO(ret!= ESP_OK, EXIT, "Start server failed");

    /* URI handler for fetching system info */
    httpd_uri_t debug_send_uri = {
        .uri = "/debug/send",
        .method = HTTP_POST,
        .handler = debug_send_handler,
        .user_ctx = rest_context
    };

    httpd_register_uri_handler(server, &debug_send_uri);

    static const httpd_uri_t debug_recv_uri = {
        .uri        = "/debug/recv",
        .method     = HTTP_GET,
        .handler    = debug_recv_handler,
        .user_ctx   = NULL,
        .is_websocket = true
    };

    httpd_register_uri_handler(server, &debug_recv_uri);


    static const httpd_uri_t ota_data_uri = {
        .uri      = "/ota/data",
        .method   = HTTP_POST,
        .handler  = ota_data_handler,
        .user_ctx = NULL,
    };

    httpd_register_uri_handler(server, &ota_data_uri);

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    while (dnserv_init(&ipaddr, DNS_SERVER_PORT, dns_query_proc) != ERR_OK);

    return ESP_OK;

EXIT:
    free(rest_context);
    return ret;
}
