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
