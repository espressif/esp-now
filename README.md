# ESP-NOW Component

[![Component Registry](https://components.espressif.com/components/espressif/esp-now/badge.svg)](https://components.espressif.com/components/espressif/esp-now)

- [User Guide](https://github.com/espressif/esp-now/tree/master/User_Guide.md)

esp-now supports one-to-many and many-to-many device connection and control which can be used for the mass data transmission, like network config, firmware upgrade and debugging etc.

## Example

- [examples/control]https://github.com/espressif/esp-now/tree/master/examples/control): The device based on the ESP-NOW control can control lights on responder devices through button on initiator device.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^2.0.0:control"
```

- [examples/ota]https://github.com/espressif/esp-now/tree/master/examples/ota): The device based on the ESP-NOW ota can upgrade multiple responder devices at same time.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^2.0.0:ota"
```

- [examples/provisioning]https://github.com/espressif/esp-now/tree/master/examples/provisioning): The device based on the ESP-NOW provisioning can provision WiFi on initiator device through APP and then configure multiple responders WiFi network at same time.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^2.0.0:provisioning"
```

- [examples/security]https://github.com/espressif/esp-now/tree/master/examples/security): The device based on the ESP-NOW security can encrypt the communication data with ECDH and AES128-CCM.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^2.0.0:security"
```

- [examples/solution](https://github.com/espressif/esp-now/tree/master/examples/solution): The device based on the ESP-NOW solution can provision WiFi on initiator device through APP and then configure responders WiFi network, control lights on responder devices through button on initiator device, upgrade responder devices etc.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^2.0.0:solution"
```

- [examples/wireless_debug]https://github.com/espressif/esp-now/tree/master/examples/wireless_debug): The device based on the ESP-NOW wireless_debug can debug the ESP-NOW devices.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^2.0.0:wireless_debug"
```

> Note: For the examples downloaded by using this command, you need to comment out the override_path line in the main/idf_component.yml.