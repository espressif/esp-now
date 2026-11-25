# v2.5.4

Doc:
- docs: Describe the scope and limitations of ESP-NOW encryption in the User Guide, and the recommended security configuration in espnow.h.

Fixed:
- Fix the receive callback blocking the Wi-Fi task when the event queue is full; the frame is now dropped and not acknowledged.
- Fix sending encrypted frames concurrently from multiple tasks, which could make espnow_send() fail or send frames that the receiver cannot decrypt.
- Fix espnow_send() returning ESP_ERR_TIMEOUT before wait_ticks has elapsed while waiting for another send to finish, when the FreeRTOS tick rate is below 1000 Hz.
- Fix frames with forward_ttl set to ESPNOW_FORWARD_MAX_COUNT (31) being forwarded without the hop count being decremented.
- Fix a security responder with an unfinished handshake ignoring all other initiators indefinitely.
- Fix the custom_data of the Wi-Fi configuration passed to espnow_prov_responder_start() not being sent to the initiator.
- Fix espnow_prov_initiator_send() returning ESP_ERR_INVALID_ARG with ESP-IDF v5.4 and later when CONFIG_ESPNOW_APP_SECURITY is enabled.
- Fix ESP_LOGx output not being captured by the ESP-NOW, flash and custom log sinks with ESP-IDF v5.5 and later.
- Tighten the validation of received security, control, provisioning and OTA frames to reject frames that are too short or carry invalid field values.
- Fix a build error with ESP-IDF versions earlier than v5.2 when the FreeRTOS trace facility and stats formatting functions are enabled. [#161](https://github.com/espressif/esp-now/issues/161)
- Fix espnow_log_init() failing in the wireless_debug example because the default log file size exceeded the log partition of the example.

Features:
- Support ESP-NOW v2.0 frames with ESP-IDF v5.4 and later; ESPNOW_PAYLOAD_LEN is derived from ESP_NOW_MAX_DATA_LEN_V2 instead of being fixed at 230 bytes. Buffers sized with ESPNOW_PAYLOAD_LEN or ESPNOW_DATA_LEN grow accordingly; the frame header layout and the OTA packet size are unchanged.
- Add CONFIG_ESPNOW_SEC_REJECT_PLAINTEXT (disabled by default) to drop received plaintext frames of data types that are configured as secured.
- Add espnow_set_security_required_for_data_type() and espnow_get_security_required_for_data_type() to set and query the data types to which CONFIG_ESPNOW_SEC_REJECT_PLAINTEXT applies.
- Add CONFIG_ESPNOW_SEC_IGNORE_REST (disabled by default) to make the security responder ignore REST frames, which are not authenticated.
- Add CONFIG_ESPNOW_TIME_SECURITY (disabled by default) to encrypt time synchronization data without enabling CONFIG_ESPNOW_ALL_SECURITY.
- Add CONFIG_ESPNOW_SEC_HANDSHAKE_TIMEOUT_MS (default 10000) to set the time after which an unfinished handshake on the responder can be replaced by a handshake from another initiator.
- Add CONFIG_ESPNOW_SEC_SCAN_MAX_NUM and CONFIG_ESPNOW_OTA_SCAN_MAX_NUM (default 256 each) to limit the number of responders recorded in a security or OTA scan.
- Add CONFIG_ESPNOW_GROUP_COUNT_MAX (default 32) to limit the number of application groups a device can join; the oldest application group is replaced when the limit is reached.
- Add CONFIG_ESPNOW_OTA_FIRMWARE_SIZE_MAX (default 4 MB) to set the maximum firmware size supported by the OTA responder.
- Add CONFIG_ESPNOW_OTA_RESTART_MIN_INTERVAL_MS (default 1000) to set the minimum interval between OTA session restarts triggered by STATUS frames on the responder.
- Add CONFIG_ESPNOW_DEBUG_LOG_FLASH_ERASE_MIN_INTERVAL_MS (default 0, disabled) to set the minimum interval between flash log file rotations; log lines that would require an earlier rotation are dropped.

# v2.5.3

Doc:
- docs: Translate CN ESP-NOW User Guide.
- docs: Modify docs latest branch name.
- docs: Add deploy docs.
- docs: Update espnow_ctrl.h. [#135](https://github.com/espressif/esp-now/pull/135)

Fixed:
- Fix an incompatible pointer type issue. [#145](https://github.com/espressif/esp-now/issues/145)
- Fix compile issue when update IDF to 6.0.
- Improve WebUI accessibility and device info retrieval.
- Update espnow_send_cb to support different ESP-IDF versions.
- Fix the insufficient clearance of sec responder stop.
- Fix web end cannot load the device correctly.
- Fix coin cell build broken due to new button component.
- Fix bug when removing item from bindlist.
- Fix bug when espnow data size is validated for internal consistency but not against the actual received packet length.
- Fix deprecated wakeup cause retrieval allowing multiple wakeup reasons. [#153](https://github.com/espressif/esp-now/pull/153)
- Fix spelling errors in code and documentation (forward, provisioning, length).

Features:
- Allow configuration of espnow_main task stack size and priority, support time sync. [#140](https://github.com/espressif/esp-now/issues/140) [#143](https://github.com/espressif/esp-now/issues/143) [#147](https://github.com/espressif/esp-now/issues/147)
- Add coin cell button v2.
- Add codespell check to CI pipeline for automatic spelling error detection.

# v2.5.2

Doc:
- docs: Update Support Periods.

Fixed:
- Fix log output error of security example. [#120](https://github.com/espressif/esp-now/pull/120)
- Fix an issue of unable to receive data when enable forward and retransmission times are too large.
- Fix a bug when upgrade multiple firmware on once session. [#122](https://github.com/espressif/esp-now/issues/122)
- Fix an Out-of-Bound (OOB) vulnerability for group type message.
- Fix a replay attacks vulnerability for ciphertext.
- Fix devkit power save mode and update README of coin_cell_demo example.

Features:
- Add bind error event for timeout, rssi and bindlist size.
- Reduce the number of NVS operations. [#123](https://github.com/espressif/esp-now/pull/123)
- Use separate cache to store security keys, one for encrypt, the other for decrypt.
- Announce security handshake info of initiator to responder.

# v2.5.1

Doc:
- docs: Update application note with country channel change.

Fixed:
- Fix: missing successful MAC list when exchanging keys.[#113](https://github.com/espressif/esp-now/issues/113)
- Fix: Fix an issue of OTA data send
- Fix: Fix an compile issue
- Fix: AEG-1318: ESP-NOW may miss WIFI_EVENT_STA_CONNECTED
- Fix: readme remove incorrect notes

# v2.5.0

Features:
- Control attribute is mostly meaningless. [#82](https://github.com/espressif/esp-now/issues/82)
- Control functionality improvements. [#84](https://github.com/espressif/esp-now/issues/84)
- Update document for match the components. [#85](https://github.com/espressif/esp-now/issues/85)
- Support USB Serial JTAG and USB CDC console. [#86](https://github.com/espressif/esp-now/issues/86)
- Add Kconfig options for esp-now OTA. [#94](https://github.com/espressif/esp-now/issues/94)
- Add ESP32C6 supported. [#97](https://github.com/espressif/esp-now/issues/97)
- Obtain vue project sources to web server of wireless_debug example.[#100](https://github.com/espressif/esp-now/issues/100)
- Change ESP-IDF version to v4.4.x.

Fixed:

- Fix provisioning doesn't work when enable CONFIG_ESPNOW_APP_SECURITY.[#103](https://github.com/espressif/esp-now/issues/103)
- Fix an issue of init error after deinit.[#104](https://github.com/espressif/esp-now/issues/104)
- Fix Wrong channel logged on send error. [#106](https://github.com/espressif/esp-now/issues/106)
- Fix Some API is deprecated which cause compile warning. [#111](https://github.com/espressif/esp-now/issues/111)

# v2.4.0

Features:
- Update readme for using the coin cell button to work with a ESP-NOW Matter bridge.
- Update coin cell switch default config to work with responder in power save.

Fixed:
- Fix ota spi write error when device encryption is enabled. [#80](https://github.com/espressif/esp-now/pull/80)
- Fix incorrect condition check for accepting bind request. [#81](https://github.com/espressif/esp-now/pull/81)
- Fix return type warning when compile use ESP-IDF master branch.
- Fix transmit on the default allowed channels unless a specific country is set.

# v2.3.0

Features:
- Increase wait ack config range and reduce wait ack duration for button.
- Use random IV at the end of the payload for gain a lot of security. [#40](https://github.com/espressif/esp-now/issues/40)
- Add option for enable/disable security on all components. [#71](https://github.com/espressif/esp-now/issues/71)
- Add sec event for application to get the result. [#73](https://github.com/espressif/esp-now/issues/73)

Fixed:
- Fix an issue for light sleep after backup the channel
- Fix an issue of missnamed. [#72](https://github.com/espressif/esp-now/issues/72)
- Update readme of miss actual path. [#76](https://github.com/espressif/esp-now/issues/76)

# v2.2.0

Features:
- Add an option to enable/disable responder forwarding.

Fixed:
- Fix the bug with bindlist cleanup when unbinding.
- Fix compile issue when enable Anti-rollback option.
- Fix the bug that prevents sending clear data when security is enabled. [#63](https://github.com/espressif/esp-now/issues/63)
- Fix the bug where ESPNOW_INIT_CONFIG_DEFAULT does not match its declaration in C++. [#65](https://github.com/espressif/esp-now/issues/65)

# v2.1.1

Fix:
- Fix a bug of compile fail when use component registry

# v2.1.0

Features:
- Add light sleep configuration for the low power application.
- Add auto control the channel of ESP-NOW package sending, so application data transmission will have less time.

Fix:
- Fix a bug of init function which cause security sample can't work. [#60](https://github.com/espressif/esp-now/issues/60)

Examples:
- coin_cell_demo: Provides a low power solution to achieve wireless control between `SWITCH` and `BULB`.

# v2.0.0

This is the first release version for ESP-NOW component in Espressif Component Registry, more detailed descriptions about the project, please refer to [User_Guide](https://github.com/espressif/esp-now/tree/master/User_Guide.md).

Features:
- Control: Support the simple data communication between `initiator device` and `responder devices`.
- Provision: Support to do WiFi provision for multiple devices over ESP-NOW at the same time.
- Security: Support to encrypt the application level data by ECDH and AES128-CCM.
- Debug: Support to receive the running log from responder devices for debugging.
- Upgrade: Support to upgrade multiple devices over ESP-NOW at the same time.
