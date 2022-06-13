/* Wireless Debug Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "monitor.h"

void app_main()
{
#if CONFIG_ESPNOW_DEBUG_MONITOR
    monitor();
#elif CONFIG_ESPNOW_DEBUG_MONITORED
    monitored_device();
#endif
}