ESP-NOW Internal Time Synchronization
=====================================

Overview
--------

This module provides time synchronization between ESP-NOW nodes **without requiring internet connectivity**. It is designed to solve the problem where nodes waking from deep sleep can incorrectly reset the controller node's time.

The solution uses two roles following the ESP-NOW component convention:

- **Initiator** (Controller): Broadcasts authoritative time, never adjusts its own time
- **Responder** (Data Node): Receives time broadcasts, adjusts its time accordingly

This is similar to OTA where Initiator pushes firmware and Responder receives it.

This addresses `GitHub Issue #140 <https://github.com/espressif/esp-now/issues/140>`_.

API Reference
-------------

Header File
^^^^^^^^^^^

- ``src/time/include/espnow_time.h``

Initiator API (Controller/Time Broadcaster)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. c:function:: esp_err_t espnow_time_initiator_start(const espnow_time_initiator_config_t *config)

    Start time synchronization initiator.

.. c:function:: esp_err_t espnow_time_initiator_stop(void)

    Stop time synchronization initiator.

.. c:function:: esp_err_t espnow_time_initiator_broadcast(void)

    Broadcast current time to all nodes.

Responder API (Data Node/Time Receiver)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. c:function:: esp_err_t espnow_time_responder_start(const espnow_time_responder_config_t *config)

    Start time synchronization responder.

.. c:function:: esp_err_t espnow_time_responder_stop(void)

    Stop time synchronization responder.

.. c:function:: esp_err_t espnow_time_responder_request(void)

    Request time synchronization from initiator (non-blocking).
    Use ``ESP_EVENT_ESPNOW_TIMESYNC_SYNCED`` event to get notified when sync completes.

Example Usage
-------------

Controller Node (Initiator)
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

    #include "espnow.h"
    #include "espnow_time.h"

    void app_main(void)
    {
        // Initialize WiFi and ESP-NOW first
        // ...

        espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
        espnow_init(&espnow_config);

        // Start as time initiator (controller)
        espnow_time_initiator_config_t config = {
            .sync_interval_ms = 30000,  // Broadcast time every 30 seconds
        };
        espnow_time_initiator_start(&config);

        // Controller broadcasts time and responds to requests
        // It never adjusts its own time based on other nodes
    }

Data Node (Responder)
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

    #include "espnow.h"
    #include "espnow_time.h"
    #include "esp_sleep.h"

    static int64_t s_time_offset_us = 0;

    static void timesync_event_handler(void *arg, esp_event_base_t base,
                                       int32_t event_id, void *event_data)
    {
        if (event_id == ESP_EVENT_ESPNOW_TIMESYNC_SYNCED) {
            espnow_timesync_event_t *evt = (espnow_timesync_event_t *)event_data;
            // Calculate offset for future time queries
            s_time_offset_us = evt->synced_time_us - esp_timer_get_time();
            ESP_LOGI(TAG, "Time synced, drift: %d ms", evt->drift_ms);
        }
    }

    // Get current synchronized time
    static int64_t get_synced_time_us(void)
    {
        return esp_timer_get_time() + s_time_offset_us;
    }

    void app_main(void)
    {
        // Initialize WiFi and ESP-NOW first
        // ...

        espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
        espnow_init(&espnow_config);

        // Register event handler
        esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID,
                                   timesync_event_handler, NULL);

        // Start as time responder (data node)
        espnow_time_responder_config_t config = {
            .max_drift_ms = 100,
        };
        espnow_time_responder_start(&config);

        // Request time sync (especially after deep sleep wake)
        // Event handler will be called when sync completes
        espnow_time_responder_request();

        // ... do work ...

        // Stop before deep sleep
        espnow_time_responder_stop();
        esp_deep_sleep(60 * 1000000);  // Sleep for 60 seconds
    }

Notes
-----

1. Use ``ESP_EVENT_ESPNOW_TIMESYNC_SYNCED`` event to track synchronization status instead of polling.

2. Responders should call ``espnow_time_responder_request()`` after waking from deep sleep.

3. The ``sync_interval_ms`` parameter enables periodic time broadcast. Set to 0 for on-demand only.

4. The ``max_drift_ms`` parameter prevents adjustments for minor time differences.

5. This module uses ``ESPNOW_DATA_TYPE_TIMESYNC`` for time synchronization packets.

Events
------

The time sync module posts events to the ``ESP_EVENT_ESPNOW`` event loop:

+----------------------------------------+------------------------------------------+
| Event                                  | Description                              |
+========================================+==========================================+
| ``ESP_EVENT_ESPNOW_TIMESYNC_STARTED``  | Time sync module started                 |
+----------------------------------------+------------------------------------------+
| ``ESP_EVENT_ESPNOW_TIMESYNC_STOPPED``  | Time sync module stopped                 |
+----------------------------------------+------------------------------------------+
| ``ESP_EVENT_ESPNOW_TIMESYNC_SYNCED``   | Time synchronized (responder only)       |
+----------------------------------------+------------------------------------------+
| ``ESP_EVENT_ESPNOW_TIMESYNC_TIMEOUT``  | Time sync request timeout                |
+----------------------------------------+------------------------------------------+

Event Handler Example
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

    static void timesync_event_handler(void *arg, esp_event_base_t base,
                                        int32_t event_id, void *event_data)
    {
        switch (event_id) {
            case ESP_EVENT_ESPNOW_TIMESYNC_SYNCED: {
                espnow_timesync_event_t *evt = (espnow_timesync_event_t *)event_data;
                ESP_LOGI(TAG, "Time synced from " MACSTR ", drift: %d ms",
                         MAC2STR(evt->src_addr), evt->drift_ms);
                break;
            }
            case ESP_EVENT_ESPNOW_TIMESYNC_TIMEOUT:
                ESP_LOGW(TAG, "Time sync timeout");
                break;
            default:
                break;
        }
    }

    // Register event handler
    esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID,
                               timesync_event_handler, NULL);

Comparison with Other Modules
-----------------------------

+----------------+-----------------------------------+-----------------------------------+
| Module         | Initiator                         | Responder                         |
+================+===================================+===================================+
| OTA            | Pushes firmware                   | Receives firmware                 |
+----------------+-----------------------------------+-----------------------------------+
| Provisioning   | Requests WiFi config              | Provides WiFi config              |
+----------------+-----------------------------------+-----------------------------------+
| **Time**       | **Broadcasts time (controller)**  | **Receives time (data node)**     |
+----------------+-----------------------------------+-----------------------------------+

