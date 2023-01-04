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

#pragma once

#include <protocomm_security.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Protocomm security version 1 implementation
 *
 * This is a full fledged security implementation using
 * Curve25519 key exchange and AES-256-CTR encryption
 */
extern const protocomm_security_t protocomm_client_security1;

/**
 * @brief   Generating the Command0
 *
 * @param[out]  outbuf  pointer to internally allocated output buffer,
 *                      where the Command0 is to be stored
 * @param[out]  outlen  buffer length of the allocated output buffer
 * 
 * @return
 *  - ESP_OK : Request handled successfully
 *  - ESP_FAIL : Internal error in execution of registered handler
 *  - ESP_ERR_NO_MEM : Error allocating internal resource
 *  - ESP_ERR_NOT_FOUND : Endpoint with specified name doesn't exist
 *  - ESP_ERR_INVALID_ARG : Null instance/name arguments
 */
esp_err_t write_security1_command0(uint8_t **outbuf, ssize_t *outlen);

#ifdef __cplusplus
}
#endif
