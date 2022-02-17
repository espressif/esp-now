# Control Example

By using the `BOOT` button and `RGB LED` on the development boards, this example demonstrates the interaction process between the devices, the initiator device sends the control commands and the responder devices change the LED status when receiving the control commands.

## What to expect in this example?

This example uses at least two ESP32 development boards (for example: ESP32-S2-Saola-1 or ESP32-C3-DevKitM-1), you need to use the `BOOT` button and `RGB LED` on the development boards to demonstrate.
> Note: If you are using other boards, please modify the GPIOs accordingly.

`<img src="../../docs/_static/en/esp32-s2-saola-1.png" width="550">`

- Press the `BOOT` button to send control commands
- `RGB LED` is used to display the status after receiving the commands

> Note: The roles of `initiator` and `responder` will be on the devices at the same time. When the user presses the `BOOT` button on one device, this device will act as the initiator to send commands to other devices. When other devices receive the commands, they will change the status of `RGB LED` to show that the command is handled.

### Binding

Long press (more than 1 second) the `BOOT` button on one device, this device will send the binding command. This device acts as the initiator. When other devices receive the binding command, the `RGB LED` on these devices will turn green, indicating that the devices have been successfully bound. Other devices act as the responder.

### Control

Short press (within 1 second) the `BOOT` button on the initiator device, this device will send the control command. When other devices receive the control command, the status of `RGB LED` (on/off) will be controlled by the initiator device, and the color will be white.

> Note: The device can only control the bound devices, so it must be bound before control.

### Unbind

Long press (more than 5 seconds) the `BOOT` button on the initiator device, this device will send the unbinding command. When other devices receive the unbinding command, the `RGB LED` on these devices will turn red, indicating that the devices have been unbound successfully.
