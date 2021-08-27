# Get Started Examples

This example is used for ESP-NOW data communication. The device receives data through the serial port and transparently broadcasts it to all nodes through the serial port. The device that receives the data receives the data through the serial port.

## Configuration

To run this example, at least two development boards are required to test the communication between the two devices

- The default configuration is as follows:
   - Broadcast packet: send data once and retransmit 5 times, the more retransmission times, the lower the throughput rate
   - Serial port: 115200, 1, 8
- Modify the configuration You can modify the `app_main.c` directly to configure
```c
#define CONFIG_UART_PORT_NUM UART_NUM_0
#define CONFIG_UART_BAUD_RATE 115200
#define CONFIG_UART_TX_IO UART_PIN_NO_CHANGE
#define CONFIG_UART_RX_IO UART_PIN_NO_CHANGE
#define CONFIG_RETRY_NUM 5
```
## Run

<div align=center>
<img src="../../docs/_static/en/device_log.png" width="550">
<p> Packet receiving device log </p>
</div>

<div align=center>
<img src="../../docs/_static/en/serial_port.png" width="550">
<p> The log of the sending device </p>
</div>
