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
- Announce securiy handshake info of initiator to responder.

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
- Control functionality improvments. [#84](https://github.com/espressif/esp-now/issues/84)
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
- Fix Some API is deprecated which cause compile waring. [#111](https://github.com/espressif/esp-now/issues/111)

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
- Fix compile issue when enbale Anti-rollback option.
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
