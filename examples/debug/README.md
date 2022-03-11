# Debug Log Package Upload Example

This example demonstrates how to debug the ESP-NOW devices.

## Functionality

This example introduces how to debug the device through uart or espnow command, and send the log data to server and other espnow devices.

- A series of `console` control commands are registered, and the basic information of the device can be obtained through the serial port command or espnow command sending from other device.
- The device log data will be stored in flash and sent through espnow data.
- This example also implements the function of transferring log data to the TCP server on the http network. The data is packaged into a file and transmitted to server, and uploaded according to the size of the data `ESPNOW_DATA_LEN`.

## Hardware Required

This example can be executed on any platform board.

## How to Use the Example

### Run HTTP server

A built-in python HTTP server can be used for the example.
Open a new terminal to run the HTTP server, and then run the below command to start the server.
```shell
python -m SimpleHTTPServer 8070
```
> Note:
> 
	1. The above command may vary from systems. For some systems, the command line may be `python2 -m SimpleHTTPServer 8070`.
	2. If there are firewall softwares that prevent any access to the port 8070, please grant the access while the example is running.

### Configure the devices

Navigate to the debug example directory, and type `idf.py menuconfig` to configure the debug example. 

Set following parameters under `Example Connection Configuration` Options:
* Set `WiFi SSID` of the Router (Access-Point).
* Set `WiFi Password` of the Router (Access-Point).

Set following parameter under `Example Configuration` Options:
* Set the server URL as manage the debug log information.
  
```
http://<host-ip-address>:<host-port>

for e.g,
http://192.168.0.3:8070/flash_log
```

### Operating procedures

1. The user needs to start HTTP server and connect to the network.
2. The user controls the deivce through the console command line, and the `help` command can show the format of the input.

## Example Output

Output sample of command `system_info -i` `log -i` `help` is as follows:
```
esp32c3> system_info -i
I (12714) app_main: System information, mac: 7c:df:a1:76:42:38, channel: 1, free heap: 204640

esp32c3> log -i
I (426314) log_func: log level, uart: INFO, espnow: INFO, flash: INFO, custom: NONE

esp32c3> help
help 
  Print the list of registered commands

version 
  Get version of chip and SDK

heap 
  Get the current size of free heap memory

restart  [-i]
  Reset of the chip
    -i, --info  Get restart information

reset 
  Clear device configuration information

time  [-g] [-s <utc>] [-z <time_zone>]
  time common configuration
  -s, --set=<utc>  Set system time
     -g, --get  Get system time
  -z, --time_zone=<time_zone>  Time zone

log  [-i] [-t <tag>] [-l <level>] [-m <mode('uart', 'flash', 'espnow' or 'custom')>] [-f <operation ('size', 'data' or 'espnow')>]
  Set log level for given tag
  -t, --tag=<tag>  Tag of the log entries to enable, '*' resets log level for all tags to the given value
  -l, --level=<level>  Selects log level to enable (NONE, ERR, WARN, INFO, DEBUG, VER)
  -m, --mode=<mode('uart', 'flash', 'espnow' or 'custom')>  Selects log to mode ('uart', 'flash', 'espnow' or 'custom')
  -f, --flash=<operation ('size', 'data' or 'espnow')>  Read to the flash of log information
    -i, --info  Configuration of output log

beacon 
  Send ESP-NOW broadcast to let other devices discover

fallback 
  Upgrade error back to previous version

coredump  [-loe]
  Get core dump information
  -l, --length  Get coredump data length
  -o, --output  Read the coredump data of the device
   -e, --erase  Erase the coredump data of the device

wifi_config  [-i] [-C <country_code ('CN', 'JP, 'US')>] [-c <channel (1 ~ 14)>] [-s <ssid>] [-b <bssid (xx:xx:xx:xx:xx:xx)>] [-p <password>] [-t <power (8 ~ 84)>]
  Set the configuration of the ESP32 STA
  -C, --country_code=<country_code ('CN', 'JP, 'US')>  Set the current country code
  -c, --channel=<channel (1 ~ 14)>  Set primary channel
  -s, --ssid=<ssid>  SSID of router
  -b, --bssid=<bssid (xx:xx:xx:xx:xx:xx)>  BSSID of router
  -p, --password=<password>  Password of router
  -t, --tx_power=<power (8 ~ 84)>  Set maximum transmitting power after WiFi start.
    -i, --info  Get Wi-Fi configuration information

gpio  [-c <num>] [-s <num>] [-g <num>] [-l <0 or 1>]
  GPIO common configuration
  -c, --config=<num>  GPIO common configuration
  -s, --set=<num>  GPIO set output level
  -g, --get=<num>  GPIO get input level
  -l, --level=<0 or 1>  level. 0: low ; 1: high

uart  [-s] [--tx_io=<tx_io_num>] [--rx_io=<rx_io_num>] [-p <0 | 1 | 2>] [-b <baud_rate>] [-r timeout_ms] [-w data]
  uart common configuration
   -s, --start  Install UART driver and set the UART to the default configuration
  --tx_io=<tx_io_num>  UART TX pin GPIO number
  --rx_io=<rx_io_num>  UART RX pin GPIO number
  -p, --port_num=<0 | 1 | 2>  Set UART port number
  -b, --baud_rate=<baud_rate>  Set UART baud rate
  -r, --read=timeout_ms  UART read bytes from UART buffer
  -w, --write=data  UART write bytes from UART buffer

system_info  [-i]
  Print the system information
    -i, --info  Print the system information

```
## Note

1. ESP32 and phone should be connected to the same router, so that data information can be collected on the network assistant tool. 
2. It should also be noted that when using a new chip for burning, the chip needs to be erased and then burned.
3. The log_info area is added to the partition table in this example as a space to save logs. The chip needs to be erased before programming.
4. The head of the data is a timestamp. It is just an experiment and there is no real-time calibration. It can be modified according to the user's own needs.
5. Get the status of log mode `uart`, `flash`, `espnow` in the console command `log -i`. The `console` command can't be used to disable the log configuration, because the main program has been forced to be enabled. If the user do not want to use such a function, the main program can be disable.
```
// espnow_log_init(&log_config);
```
6. The [wireless_debug](../examples/wireless_debug) example can be used to monitor the debug device by sending commands and receiving logs through espnow data. 