/* Wireless Debug Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"
#include "monitor.h"

void app_main()
{
#if CONFIG_APP_ESPNOW_DEBUG_MONITOR
    app_espnow_monitor_device_start();
#elif CONFIG_APP_ESPNOW_DEBUG_MONITORED
    app_espnow_monitored_device_start();
#endif
}