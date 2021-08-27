# Control Example

Demonstrate through the `BOOT` button and `RGB LED` on the development board, the interaction process between the device that initiates the control command and the controlled device

<img src="../../docs/_static/en/esp32-s2-saola-1.png" width="550">

## What to expect in this example?

This example uses at least two ESP32 development boards (for example: ESP32-S2-Saola-1 or ESP32-C3-DevKitM-1), you need to use the BOOT button and RGB LED on the development board to demonstrate
<img src="../../docs/_static/en/esp32-s2-saola-1.png" width="550">

- Press the `BOOT` button to send instructions
- `RGB LED` is used to display the status after the received command

> Note: The roles of `initiator` and `responder` will run on the device at the same time. When the device presses `BOOT` and acts as the initiator to send instructions to other devices, after other devices receive the instructions, they will pass the `RGB LED' `To display command changes.

### Binding
Press and hold the `BOOT` button for more than 1 second. After releasing the `BOOT` button, send the binding command. After other devices receive the binding command, the `RGB LED` will turn green, indicating that the device has been successfully bound
### control
Press and hold the `BOOT` button within 1 second to send a control command. Press and hold the `BOOT` button for the first time to turn off the `RGB LED`, and hold down the `BOOT` button for the second time to turn on the `RGB LED`

> Note: The device can only control the bound device, so it must be bound before control.

### Unbind
Press and hold the `BOOT` button for more than 5 seconds. After releasing the `BOOT` button, send an unbinding instruction. After other devices receive the unbinding instruction, the RGB LED will turn red, indicating that the device has been unbound successfully
