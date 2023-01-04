# ESP-NOW Component

- [User Guide](https://github.com/espressif/esp-now/tree/master/User_Guide.md)

esp-now supports one-to-many and many-to-many device connection and control which can be used for the mass data transmission, like network config, firmware upgrade and debugging etc.

## Example

- [examples/solution](https://github.com/espressif/esp-now/tree/master/examples/solution): The device based on the ESP-NOW solution can provision WiFi on initiator device through APP and then configure responders WiFi network, control lights on responder devices through button on initiator device, upgrade responder devices etc.

You can create a project from this example by the following command:

```
idf.py create-project-from-example "espressif/esp-now^0.1.0:solution"
```

> Note: For the examples downloaded by using this command, you need to comment out the override_path line in the main/idf_component.yml.