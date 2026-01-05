// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>

#include <protocomm_client_security1.h>

#if ESPNOW_USE_PSA_CRYPTO
#include <psa/crypto.h>
#else
#include <mbedtls/ccm.h>
#endif

#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_security.h"

static const char* TAG = "espnow_sec";

#if ESPNOW_USE_PSA_CRYPTO
typedef struct {
    psa_key_id_t key_id;
    bool crypto_inited;
} espnow_psa_ctx_t;

static esp_err_t espnow_psa_init_once(espnow_psa_ctx_t *ctx)
{
    if (ctx->crypto_inited) {
        return ESP_OK;
    }
    psa_status_t st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int)st);
        return ESP_FAIL;
    }
    ctx->crypto_inited = true;
    ctx->key_id = (psa_key_id_t)0;
    return ESP_OK;
}
#endif

esp_err_t espnow_sec_init(espnow_sec_t *sec)
{
    ESP_PARAM_CHECK(sec);

    memset(sec, 0, sizeof(espnow_sec_t));
#if ESPNOW_USE_PSA_CRYPTO
    sec->cipher_ctx = ESP_CALLOC(1, sizeof(espnow_psa_ctx_t));
#else
    sec->cipher_ctx = (mbedtls_ccm_context *)ESP_CALLOC(1, sizeof(mbedtls_ccm_context));
#endif
    sec->key_len = KEY_LEN;
    sec->iv_len = IV_LEN;
    sec->tag_len = TAG_LEN;

#if ESPNOW_USE_PSA_CRYPTO
    ESP_ERROR_RETURN(!sec->cipher_ctx, ESP_ERR_NO_MEM, "calloc psa ctx failed");
    if (espnow_psa_init_once((espnow_psa_ctx_t *)sec->cipher_ctx) != ESP_OK) {
        ESP_LOGE(TAG, "psa init failed");
        return ESP_FAIL;
    }
#else
    mbedtls_ccm_init((mbedtls_ccm_context *)sec->cipher_ctx);
#endif

    return ESP_OK;
}

esp_err_t espnow_sec_deinit(espnow_sec_t *sec)
{
    ESP_PARAM_CHECK(sec);

    if (sec->cipher_ctx) {
#if ESPNOW_USE_PSA_CRYPTO
        espnow_psa_ctx_t *ctx = (espnow_psa_ctx_t *)sec->cipher_ctx;
        if (ctx->key_id != (psa_key_id_t)0) {
            (void)psa_destroy_key(ctx->key_id);
            ctx->key_id = (psa_key_id_t)0;
        }
        ESP_FREE(sec->cipher_ctx);
#else
        mbedtls_ccm_free((mbedtls_ccm_context *)sec->cipher_ctx);
        ESP_FREE(sec->cipher_ctx);
#endif
    }
    memset(sec, 0, sizeof(espnow_sec_t));

    return ESP_OK;
}

esp_err_t espnow_sec_setkey(espnow_sec_t *sec, uint8_t app_key[APP_KEY_LEN])
{
    ESP_PARAM_CHECK(sec);
    ESP_PARAM_CHECK(app_key);
    ESP_PARAM_CHECK(sec->cipher_ctx);

#if ESPNOW_USE_PSA_CRYPTO
    espnow_psa_ctx_t *ctx = (espnow_psa_ctx_t *)sec->cipher_ctx;
    if (ctx->key_id != (psa_key_id_t)0) {
        (void)psa_destroy_key(ctx->key_id);
        ctx->key_id = (psa_key_id_t)0;
    }

    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, 8 * sec->key_len);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, sec->tag_len));

    psa_status_t st = psa_import_key(&attr, app_key, sec->key_len, &ctx->key_id);
    psa_reset_key_attributes(&attr);
    ESP_ERROR_RETURN(st != PSA_SUCCESS, ESP_FAIL, "psa_import_key failed: %d", (int)st);
#else
    int ret = mbedtls_ccm_setkey((mbedtls_ccm_context *)sec->cipher_ctx, MBEDTLS_CIPHER_ID_AES, app_key, 8 * sec->key_len);
    ESP_ERROR_RETURN(ret != ESP_OK, ret, "mbedtls_ccm_setkey %x", ret);
#endif

    memcpy(sec->key, app_key, sec->key_len);
    memcpy(sec->iv, app_key + sec->key_len, sec->iv_len);
    sec->state = ESPNOW_SEC_OVER;

    return ESP_OK;
}

esp_err_t espnow_sec_auth_encrypt(espnow_sec_t *sec, const uint8_t *input, size_t ilen,
                    uint8_t *output, size_t output_len,
                    size_t *olen, size_t tag_len)
{
    ESP_PARAM_CHECK(sec);
    ESP_PARAM_CHECK(input);
    ESP_PARAM_CHECK(ilen);
    ESP_PARAM_CHECK(output);
    ESP_PARAM_CHECK(olen);
    ESP_PARAM_CHECK(output_len >= ilen + tag_len);
    ESP_PARAM_CHECK(tag_len);

    if (sec->state != ESPNOW_SEC_OVER) {
        ESP_LOGE(TAG, "Security state is not over");
        return ESP_FAIL;
    }

#if ESPNOW_USE_PSA_CRYPTO
    espnow_psa_ctx_t *ctx = (espnow_psa_ctx_t *)sec->cipher_ctx;
    psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, tag_len);
    size_t out_len = 0;
    psa_status_t st = psa_aead_encrypt(ctx->key_id, alg,
                                       sec->iv, sec->iv_len,
                                       NULL, 0,
                                       input, ilen,
                                       output, output_len,
                                       &out_len);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_aead_encrypt failed: %d", (int)st);
        return ESP_FAIL;
    }
    *olen = out_len;
    return ESP_OK;
#else
    int ret = mbedtls_ccm_encrypt_and_tag((mbedtls_ccm_context *)sec->cipher_ctx, ilen, sec->iv, sec->iv_len, NULL, 0,
                                    input, output, output + ilen, tag_len);
    *olen = ilen + tag_len;

    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ccm_encrypt_and_tag with error code : %d", ret);
        return ESP_FAIL;
    }

    return ret;
#endif
}

esp_err_t espnow_sec_auth_decrypt(espnow_sec_t *sec, const uint8_t *input, size_t ilen,
                    uint8_t *output, size_t output_len,
                    size_t *olen, size_t tag_len)
{
    ESP_PARAM_CHECK(sec);
    ESP_PARAM_CHECK(input);
    ESP_PARAM_CHECK(ilen);
    ESP_PARAM_CHECK(output);
    ESP_PARAM_CHECK(olen);
    ESP_PARAM_CHECK(ilen > tag_len);
    ESP_PARAM_CHECK(output_len >= ilen - tag_len);
    ESP_PARAM_CHECK(tag_len);

    if (sec->state != ESPNOW_SEC_OVER) {
        ESP_LOGE(TAG, "Security state is not over");
        return ESP_FAIL;
    }

#if ESPNOW_USE_PSA_CRYPTO
    espnow_psa_ctx_t *ctx = (espnow_psa_ctx_t *)sec->cipher_ctx;
    psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, tag_len);
    size_t out_len = 0;
    psa_status_t st = psa_aead_decrypt(ctx->key_id, alg,
                                       sec->iv, sec->iv_len,
                                       NULL, 0,
                                       input, ilen,
                                       output, output_len,
                                       &out_len);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_aead_decrypt failed: %d", (int)st);
        return ESP_FAIL;
    }
    *olen = out_len;
    return ESP_OK;
#else
    int ret = ESP_OK;
    ilen -= tag_len;
    ret = mbedtls_ccm_auth_decrypt((mbedtls_ccm_context *)sec->cipher_ctx, ilen,
                                        sec->iv, sec->iv_len, NULL, 0,
                                        input, output, input + ilen, tag_len);
    *olen = ilen;

    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ccm_auth_decrypt with error code : %d", ret);
        return ESP_FAIL;
    }

    return ret;
#endif
}
