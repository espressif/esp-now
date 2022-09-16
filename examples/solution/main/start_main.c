/* Solution Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_utils.h"
#include "espnow.h"

#if CONFIG_ESPNOW_INITIATOR
#include "initiator.h"
#elif CONFIG_ESPNOW_RESPONDER
#include "responder.h"
#endif

void start()
{
#if CONFIG_ESPNOW_INITIATOR
    espnow_initiator();
#elif CONFIG_ESPNOW_RESPONDER
    espnow_responder();
#endif
}