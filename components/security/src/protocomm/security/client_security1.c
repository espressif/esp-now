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

#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl_internal.h>

#include <protocomm_security.h>
#include <protocomm_client_security1.h>

#include "session.pb-c.h"
#include "sec1.pb-c.h"
#include "constants.pb-c.h"

static const char* TAG = "client_security1";

#define PUBLIC_KEY_LEN  32
#define SZ_RANDOM       16

#define SESSION_STATE_RESP0  0 /* Session is not setup */
#define SESSION_STATE_RESP1  1 /* Session is not setup */
#define SESSION_STATE_DONE   2 /* Session setup successful */

    /* mbedtls context data for Curve25519 */
typedef struct public_session{
    mbedtls_ecdh_context ctx_client;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    uint8_t client_pubkey[PUBLIC_KEY_LEN];
} public_session_t;

static public_session_t *pub_session = NULL;

typedef struct session {
    /* Session data */
    uint32_t id;
    uint8_t state;
    uint8_t device_pubkey[PUBLIC_KEY_LEN];
    uint8_t sym_key[PUBLIC_KEY_LEN];
    uint8_t rand[SZ_RANDOM];

    /* mbedtls context data for AES */
    mbedtls_aes_context ctx_aes;
    unsigned char stb[16];
    size_t nc_off;
} session_t;

static void flip_endian(uint8_t *data, size_t len)
{
    uint8_t swp_buf;
    for (int i = 0; i < len/2; i++) {
        swp_buf = data[i];
        data[i] = data[len - i - 1];
        data[len - i - 1] = swp_buf;
    }
}

static void hexdump(const char *msg, uint8_t *buf, int len)
{
    ESP_LOGD(TAG, "%s:", msg);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_DEBUG);
}

static esp_err_t prepare_command0(public_session_t *session, SessionData *req)
{
    Sec1Payload *in = (Sec1Payload *) malloc(sizeof(Sec1Payload));
    if (in == NULL) {
        ESP_LOGE(TAG, "Error allocating memory for request");
        return ESP_ERR_NO_MEM;
    }

    SessionCmd0 *in_req = (SessionCmd0 *) malloc(sizeof(SessionCmd0));
    if (in_req == NULL) {
        ESP_LOGE(TAG, "Error allocating memory for request");
        free(in);
        return ESP_ERR_NO_MEM;
    }

    sec1_payload__init(in);
    session_cmd0__init(in_req);

    in_req->client_pubkey.data = pub_session->client_pubkey;
    in_req->client_pubkey.len = PUBLIC_KEY_LEN;

    in->msg = SEC1_MSG_TYPE__Session_Command0;
    in->payload_case = SEC1_PAYLOAD__PAYLOAD_SC0;
    in->sc0 = in_req;

    req->proto_case = SESSION_DATA__PROTO_SEC1;
    req->sec_ver = protocomm_client_security1.ver;
    req->sec1 = in;

    return ESP_OK;
}

static void cleanup_command0(SessionData *req)
{
    free(req->sec1->sc0);
    free(req->sec1);
}

static esp_err_t write_session_command0(public_session_t *session, uint8_t **outbuf, ssize_t *outlen)
{
    ESP_LOGD(TAG, "Start to write setup0_command");

    if (!session) {
        ESP_LOGE(TAG, "Invalid session context data");
        return ESP_ERR_INVALID_ARG;
    }

    int ret = ESP_FAIL;
    SessionData req;

    mbedtls_ecdh_init(&session->ctx_client);
    mbedtls_ctr_drbg_init(&session->ctr_drbg);

    mbedtls_entropy_init(&session->entropy);
    ret = mbedtls_ctr_drbg_seed(&session->ctr_drbg, mbedtls_entropy_func,
                                &session->entropy, NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ctr_drbg_seed with error code : %d", ret);
        goto exit_cmd0;
    }

    ret = mbedtls_ecp_group_load(&session->ctx_client.grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ecp_group_load with error code : %d", ret);
        goto exit_cmd0;
    }

    ret = mbedtls_ecdh_gen_public(&session->ctx_client.grp,
                                  &session->ctx_client.d,
                                  &session->ctx_client.Q,
                                  mbedtls_ctr_drbg_random,
                                  &session->ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ecdh_gen_public with error code : %d", ret);
        goto exit_cmd0;
    }

    ret = mbedtls_mpi_write_binary(&session->ctx_client.Q.X,
                                   session->client_pubkey,
                                   PUBLIC_KEY_LEN);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_mpi_write_binary with error code : %d", ret);
        goto exit_cmd0;
    }
    flip_endian(session->client_pubkey, PUBLIC_KEY_LEN);

    /*********** Transaction0 - SessionCmd0 ****************/
    session_data__init(&req);
    if (prepare_command0(session, &req) != ESP_OK) {
        ESP_LOGE(TAG, "Failed in prepare_command0");
        goto exit_cmd0;
    }

    *outlen = session_data__get_packed_size(&req);
    *outbuf = (uint8_t *) malloc(*outlen);
    if (!*outbuf) {
        ESP_LOGE(TAG, "Failed to allocate outbuf");
        cleanup_command0(&req);
        goto exit_cmd0;
    }

    session_data__pack(&req, *outbuf);
    cleanup_command0(&req);

    ESP_LOGD(TAG, "Write setup0_command done");

    return ESP_OK;

exit_cmd0:

    mbedtls_ecdh_free(&session->ctx_client);
    mbedtls_ctr_drbg_free(&session->ctr_drbg);
    mbedtls_entropy_free(&session->entropy);

    ESP_LOGD(TAG, "Write setup0_command failed");

    return ESP_FAIL;
}


static esp_err_t verify_response1(session_t *session, SessionData *resp)
{
    uint8_t *cli_pubkey = pub_session->client_pubkey;
    uint8_t *dev_pubkey = session->device_pubkey;

    hexdump("Device pubkey", dev_pubkey, PUBLIC_KEY_LEN);
    hexdump("Client pubkey", cli_pubkey, PUBLIC_KEY_LEN);

    if ((resp->proto_case != SESSION_DATA__PROTO_SEC1) ||
        (resp->sec1->msg  != SEC1_MSG_TYPE__Session_Response1)) {
        ESP_LOGE(TAG, "Invalid response type");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t check_buf[PUBLIC_KEY_LEN];
    Sec1Payload *in = (Sec1Payload *) resp->sec1;

    int ret = mbedtls_aes_crypt_ctr(&session->ctx_aes, PUBLIC_KEY_LEN,
                                    &session->nc_off, session->rand, session->stb,
                                    in->sr1->device_verify_data.data, check_buf);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_aes_crypt_ctr with erro code : %d", ret);
        return ESP_FAIL;
    }
    hexdump("Dec Device verifier", check_buf, sizeof(check_buf));

    if (memcmp(check_buf, pub_session->client_pubkey, sizeof(pub_session->client_pubkey)) != 0) {
        ESP_LOGE(TAG, "Key mismatch. Close connection");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t handle_session_response1(session_t *cur_session,
                                         uint32_t session_id,
                                         SessionData *resp, SessionData *req)
{
    ESP_LOGD(TAG, "Request to handle setup1_response");

    if (cur_session->state != SESSION_STATE_RESP1) {
        ESP_LOGE(TAG, "Invalid state of session %d (expected %d)", SESSION_STATE_RESP1, cur_session->state);
        return ESP_ERR_INVALID_STATE;
    }

    if (verify_response1(cur_session, resp) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid response 1");
        return ESP_FAIL;
    }

    cur_session->state = SESSION_STATE_DONE;
    ESP_LOGD(TAG, "Secure session established successfully");
    return ESP_OK;
}

static esp_err_t verify_response0(session_t *session, SessionData *resp, const protocomm_security_pop_t *pop)
{
    if ((resp->proto_case != SESSION_DATA__PROTO_SEC1) ||
        (resp->sec1->msg  != SEC1_MSG_TYPE__Session_Response0)) {
        ESP_LOGE(TAG, "Invalid response type");
        return ESP_ERR_INVALID_ARG;
    }

    int ret;
    Sec1Payload *in = (Sec1Payload *) resp->sec1;

    if (in->sr0->device_pubkey.len != PUBLIC_KEY_LEN) {
        ESP_LOGE(TAG, "Device public key length as not as expected");
        return ESP_FAIL;
    }

    if (in->sr0->device_random.len != SZ_RANDOM) {
        ESP_LOGE(TAG, "Device random data length is not as expected");
        return ESP_FAIL;
    }

    uint8_t *cli_pubkey = pub_session->client_pubkey;
    uint8_t *dev_pubkey = session->device_pubkey;
    memcpy(session->device_pubkey, in->sr0->device_pubkey.data, in->sr0->device_pubkey.len);

    hexdump("Device pubkey", dev_pubkey, PUBLIC_KEY_LEN);
    hexdump("Client pubkey", cli_pubkey, PUBLIC_KEY_LEN);

    ret = mbedtls_mpi_lset(&pub_session->ctx_client.Qp.Z, 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_mpi_lset with error code : %d", ret);
        return ESP_FAIL;
    }

    flip_endian(session->device_pubkey, PUBLIC_KEY_LEN);
    ret = mbedtls_mpi_read_binary(&pub_session->ctx_client.Qp.X, dev_pubkey, PUBLIC_KEY_LEN);
    flip_endian(session->device_pubkey, PUBLIC_KEY_LEN);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_mpi_read_binary with error code : %d", ret);
        return ESP_FAIL;
    }

    ret = mbedtls_ecdh_compute_shared(&pub_session->ctx_client.grp,
                                      &pub_session->ctx_client.z,
                                      &pub_session->ctx_client.Qp,
                                      &pub_session->ctx_client.d,
                                      mbedtls_ctr_drbg_random,
                                      &pub_session->ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_ecdh_compute_shared with error code : %d", ret);
        return ESP_FAIL;
    }

    ret = mbedtls_mpi_write_binary(&pub_session->ctx_client.z, session->sym_key, PUBLIC_KEY_LEN);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_mpi_write_binary with error code : %d", ret);
        return ESP_FAIL;
    }
    flip_endian(session->sym_key, PUBLIC_KEY_LEN);

    // const protocomm_security_pop_t *pop = session->pop;
    if (pop != NULL && pop->data != NULL && pop->len != 0) {
        ESP_LOGD(TAG, "Adding proof of possession");
        uint8_t sha_out[PUBLIC_KEY_LEN];

        ret = mbedtls_sha256_ret((const unsigned char *) pop->data, pop->len, sha_out, 0);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed at mbedtls_sha256_ret with error code : %d", ret);
            return ESP_FAIL;
        }

        for (int i = 0; i < PUBLIC_KEY_LEN; i++) {
            session->sym_key[i] ^= sha_out[i];
        }
    }

    hexdump("Shared key", session->sym_key, PUBLIC_KEY_LEN);

    memcpy(session->rand, in->sr0->device_random.data, in->sr0->device_random.len);
    hexdump("Dev random", session->rand, sizeof(session->rand));
    return ESP_OK;
}

static esp_err_t prepare_command1(session_t *session, SessionData *req)
{
    int ret;
    uint8_t *outbuf = (uint8_t *) malloc(PUBLIC_KEY_LEN);
    if (!outbuf) {
        ESP_LOGE(TAG, "Error allocating ciphertext buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Initialise crypto context */
    mbedtls_aes_init(&session->ctx_aes);
    memset(session->stb, 0, sizeof(session->stb));
    session->nc_off = 0;

    ret = mbedtls_aes_setkey_enc(&session->ctx_aes, session->sym_key,
                                 sizeof(session->sym_key)*8);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_aes_setkey_enc with erro code : %d", ret);
        free(outbuf);
        return ESP_FAIL;
    }

    ret = mbedtls_aes_crypt_ctr(&session->ctx_aes, PUBLIC_KEY_LEN,
                                &session->nc_off, session->rand,
                                session->stb, session->device_pubkey, outbuf);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_aes_crypt_ctr with erro code : %d", ret);
        free(outbuf);
        return ESP_FAIL;
    }

    Sec1Payload *out = (Sec1Payload *) malloc(sizeof(Sec1Payload));
    if (!out) {
        ESP_LOGE(TAG, "Error allocating out buffer");
        free(outbuf);
        return ESP_ERR_NO_MEM;
    }
    sec1_payload__init(out);

    SessionCmd1 *out_req = (SessionCmd1 *) malloc(sizeof(SessionCmd1));
    if (!out_req) {
        ESP_LOGE(TAG, "Error allocating out_req buffer");
        free(outbuf);
        free(out);
        return ESP_ERR_NO_MEM;
    }
    session_cmd1__init(out_req);

    out_req->client_verify_data.data = outbuf;
    out_req->client_verify_data.len = PUBLIC_KEY_LEN;
    hexdump("Client verify data", outbuf, PUBLIC_KEY_LEN);

    out->msg = SEC1_MSG_TYPE__Session_Command1;
    out->payload_case = SEC1_PAYLOAD__PAYLOAD_SC1;
    out->sc1 = out_req;

    req->proto_case = SESSION_DATA__PROTO_SEC1;
    req->sec_ver = protocomm_client_security1.ver;
    req->sec1 = out;

    return ESP_OK;
}

static esp_err_t sec1_new_session(protocomm_security_handle_t handle, uint32_t session_id);

static esp_err_t handle_session_response0(session_t *cur_session,
                                         uint32_t session_id,
                                         SessionData *resp, SessionData *req,
                                         const protocomm_security_pop_t *pop)
{
    ESP_LOGD(TAG, "Request to handle setup0_response");
    esp_err_t ret;

    if (cur_session->state != SESSION_STATE_RESP0) {
        ESP_LOGW(TAG, "Invalid state of session %d (expected %d).",
                SESSION_STATE_RESP0, cur_session->state);
        // sec1_new_session(cur_session, session_id);
        return ESP_ERR_INVALID_STATE;
    }

    /*********** Transaction0 - SessionResp0 ****************/
    if (verify_response0(cur_session, resp, pop) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid response 0");
        ret = ESP_FAIL;
        goto exit_resp0;
    }

    /*********** Transaction1 - SessionCmd1 ****************/
    if (prepare_command1(cur_session, req) != ESP_OK) {
        ESP_LOGE(TAG, "Failed in prepare_command1");
        ret = ESP_FAIL;
        goto exit_resp0;
    }

    cur_session->state = SESSION_STATE_RESP1;

    ESP_LOGD(TAG, "Session setup phase1 done");
    ret = ESP_OK;

exit_resp0:

    return ret;
}

static esp_err_t sec1_session_setup(session_t *cur_session,
                                    uint32_t session_id,
                                    SessionData *req, SessionData *resp,
                                    const protocomm_security_pop_t *pop)
{
    Sec1Payload *in = (Sec1Payload *) req->sec1;
    esp_err_t ret;

    if (!in) {
        ESP_LOGE(TAG, "Empty session data");
        return ESP_ERR_INVALID_ARG;
    }

    switch (in->msg) {
        case SEC1_MSG_TYPE__Session_Response0:
            ret = handle_session_response0(cur_session, session_id, req, resp, pop);
            break;
        case SEC1_MSG_TYPE__Session_Response1:
            ret = handle_session_response1(cur_session, session_id, req, resp);
            break;
        default:
            ESP_LOGE(TAG, "Invalid security message type");
            ret = ESP_ERR_INVALID_ARG;
    }

    return ret;

}

static void sec1_session_setup_cleanup(session_t *cur_session, uint32_t session_id, SessionData *req)
{
    Sec1Payload *out = req->sec1;

    if (!out) {
        return;
    }

    switch (out->msg) {
        case SEC1_MSG_TYPE__Session_Command0:
            {
                SessionCmd0 *out_cmd0 = out->sc0;
                if (out_cmd0) {
                    free(out_cmd0);
                }
                break;
            }
        case SEC1_MSG_TYPE__Session_Command1:
            {
                SessionCmd1 *out_cmd1 = out->sc1;
                if (out_cmd1) {
                    free(out_cmd1->client_verify_data.data);
                    free(out_cmd1);
                }
                break;
            }
        default:
            break;
    }
    free(out);

    return;
}

static esp_err_t sec1_close_session(protocomm_security_handle_t handle, uint32_t session_id)
{
    session_t *cur_session = (session_t *) handle;
    if (!cur_session) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!cur_session || cur_session->id != session_id) {
        ESP_LOGE(TAG, "Attempt to close invalid session");
        return ESP_ERR_INVALID_STATE;
    }

    if (cur_session->state == SESSION_STATE_DONE) {
        /* Free AES context data */
        mbedtls_aes_free(&cur_session->ctx_aes);
    }

    memset(cur_session, 0, sizeof(session_t));
    cur_session->id = -1;
    return ESP_OK;
}

static esp_err_t sec1_new_session(protocomm_security_handle_t handle, uint32_t session_id)
{
    session_t *cur_session = (session_t *) handle;
    if (!cur_session) {
        return ESP_ERR_INVALID_ARG;
    }

    if (cur_session->id != -1) {
        /* Only one session is allowed at a time */
        ESP_LOGE(TAG, "Closing old session with id %u", cur_session->id);
        sec1_close_session(handle, session_id);
    }

    cur_session->id = session_id;
    return ESP_OK;
}

static esp_err_t pub_session_deinit(public_session_t *session)
{
    mbedtls_ecdh_free(&session->ctx_client);
    mbedtls_ctr_drbg_free(&session->ctr_drbg);
    mbedtls_entropy_free(&session->entropy);
    memset(session, 0, sizeof(public_session_t));
    free(session);
    return ESP_OK;
}

static esp_err_t sec1_init(protocomm_security_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    session_t *cur_session = (session_t *) calloc(1, sizeof(session_t));
    if (!cur_session) {
        ESP_LOGE(TAG, "Error allocating new session");
        return ESP_ERR_NO_MEM;
    }

    if (!pub_session) {
        pub_session = (public_session_t *) calloc(1, sizeof(public_session_t));
        if (!pub_session) {
            ESP_LOGE(TAG, "Error allocating memory for public session");
            free(cur_session);
            return ESP_ERR_NO_MEM;
        }
    }

    cur_session->id = -1;

    *handle = (protocomm_security_handle_t) cur_session;

    return ESP_OK;
}

static esp_err_t sec1_cleanup(protocomm_security_handle_t handle)
{
    session_t *cur_session = (session_t *) handle;
    if (cur_session) {
        sec1_close_session(handle, cur_session->id);
    }
    free(handle);
    if (pub_session) {
        pub_session_deinit(pub_session);
        pub_session = NULL;
    }
    return ESP_OK;
}

static esp_err_t sec1_decrypt(protocomm_security_handle_t handle,
                              uint32_t session_id,
                              const uint8_t *inbuf, ssize_t inlen,
                              uint8_t *outbuf, ssize_t *outlen)
{
    session_t *cur_session = (session_t *) handle;
    if (!cur_session) {
        return ESP_ERR_INVALID_ARG;
    }

    if (*outlen < inlen) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!cur_session || cur_session->id != session_id) {
        ESP_LOGE(TAG, "Session with ID %d not found", session_id);
        return ESP_ERR_INVALID_STATE;
    }

    if (cur_session->state != SESSION_STATE_DONE) {
        ESP_LOGE(TAG, "Secure session not established");
        return ESP_ERR_INVALID_STATE;
    }

    *outlen = inlen;
    int ret = mbedtls_aes_crypt_ctr(&cur_session->ctx_aes, inlen, &cur_session->nc_off,
                                    cur_session->rand, cur_session->stb, inbuf, outbuf);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed at mbedtls_aes_crypt_ctr with error code : %d", ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t sec1_req_handler(protocomm_security_handle_t handle,
                                  const protocomm_security_pop_t *pop,
                                  uint32_t session_id,
                                  const uint8_t *inbuf, ssize_t inlen,
                                  uint8_t **outbuf, ssize_t *outlen,
                                  void *priv_data)
{
    session_t *cur_session = (session_t *) handle;
    if (!cur_session) {
        ESP_LOGE(TAG, "Invalid session context data");
        return ESP_ERR_INVALID_ARG;
    }

    if (session_id != cur_session->id) {
        ESP_LOGE(TAG, "Invalid session ID : %d (expected %d)", session_id, cur_session->id);
        return ESP_ERR_INVALID_STATE;
    }

    SessionData *req;
    SessionData resp;
    esp_err_t ret;

    req = session_data__unpack(NULL, inlen, inbuf);
    if (!req) {
        ESP_LOGE(TAG, "Unable to unpack setup_req");
        return ESP_ERR_INVALID_ARG;
    }
    if (req->sec_ver != protocomm_client_security1.ver) {
        ESP_LOGE(TAG, "Security version mismatch. Closing connection");
        session_data__free_unpacked(req, NULL);
        return ESP_ERR_INVALID_ARG;
    }

    session_data__init(&resp);
    ret = sec1_session_setup(cur_session, session_id, req, &resp, pop);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Session setup error %d", ret);
        session_data__free_unpacked(req, NULL);
        return ESP_FAIL;
    }

    /* Add for security done */
    if (cur_session->state == SESSION_STATE_DONE) {
        *outlen = 0;
        *outbuf = NULL;
        return ESP_OK;
    }

    resp.sec_ver = req->sec_ver;
    session_data__free_unpacked(req, NULL);

    *outlen = session_data__get_packed_size(&resp);
    *outbuf = (uint8_t *) malloc(*outlen);
    if (!*outbuf) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    session_data__pack(&resp, *outbuf);

    sec1_session_setup_cleanup(cur_session, session_id, &resp);
    return ESP_OK;
}

const protocomm_security_t protocomm_client_security1 = {
    .ver = 1,
    .init = sec1_init,
    .cleanup = sec1_cleanup,
    .new_transport_session = sec1_new_session,
    .close_transport_session = sec1_close_session,
    .security_req_handler = sec1_req_handler,
    .encrypt = sec1_decrypt, /* Encrypt == decrypt for AES-CTR */
    .decrypt = sec1_decrypt,
};

esp_err_t write_security1_command0(uint8_t **outbuf, ssize_t *outlen)
{
    /* Add for security start */
    if (!pub_session) {
        ESP_LOGE(TAG, "Session not init");
        return ESP_ERR_INVALID_ARG;
    }

    if (write_session_command0(pub_session, outbuf, outlen) != ESP_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
