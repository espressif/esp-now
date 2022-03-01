# ESP-NOW Debug Receiver Board Demo

ESP-NOW debug receiver board receives running log and coredump data from ESP-NOW devices via [ESP-NOW](https://esp-idf.readthedocs.io/en/latest/api-reference/wifi/esp_now.html) wireless transmission technology.

## Functionality

ESP-NOW debug receiver board provides the following features:

 - [Wi-Fi configuration](#Wi-Fi-Command): sets Wi-Fi information needed in STA mode, including router SSID, password, BSSID and work channel, and saves/erases the configuration information. 
 - [Wi-Fi scan](#Wi-Fi-Command): works in STA mode, and scans AP or ESP-NOW devices nearby, sets filters such as filtered by RSSI, SSID or BSSID, and sets passive scan time in each channel. 
 - [ESP-NOW configuration](#ESP-NOW-Command): provides control, provisioning, ota, configuration and iperf commands
 - [Command](#Command-Command): runs commands on specific devices.
 - [General command](#Other-Command): includes help command to print all currently supported commands.
 - [Web server](#Web-Server): starts a HTTP web server, and PC web browser can get running log and status from the server.

> Note:
> 1. ESP-NOW debug receiver board can receive debugging data from devices only when the board is on the same Wi-Fi channel with the devices.
> 2. If the ESP-NOW debug receiver board is on the same channel with devices, you don't need to connect the board with the router.
> 3. The following code needs to be added to the monitored ESP-NOW device:
  ```c
        espnow_console_config_t console_config = {
            .monitor_command.espnow = true,
        };
        espnow_log_config_t log_config = {
            .log_level_espnow  = ESP_LOG_INFO,
        };

        espnow_console_init(&console_config);
        espnow_console_commands_register();
        espnow_log_init(&log_config);
   ```

## Hardware Required

This example can be executed on any platform board and at least two development boards are required. One is the debug receiver board and others are monitored devices.

## Configuration

Open the project configuration menu (`idf.py menuconfig`) to configure web server to debug devices through the web pages (Refer to Kconfig file).

## How to Use the Example

### Workflow

1. Compile and flash this project to an ESP32 development board;
2. Open serial port terminal and restart development board:
> Please use serial port terminals such as `minicom` to avoid some unexpected problems when using `idf.py monitor`.

3. Debug ESP-NOW by entering the following commands according to the prompts.
> The following describes the use of each command in sequence.

### Serial Port Commands

* ESP-NOW debug receiver board supports the following serial port commands: help, wifi_config, wifi_scan, wifi_ping and command, provisioning, control, ota.

* The interaction of serial port commands follows the following rules:
    1. PC sends commands to ESP-NOW debug receiver board through serial port with a baud rate of 115200.
    2. In command definition, all characters are lowercase (some options are uppercase), and strings do not need to be quoted.
    3. The elements in angle brackets <> in command description should be taken as a parameter and be replaced as the case may be.
    4. The part contained in square brackets [] or brackets () in command description is the value range that can be filled in.
    5. The pattern of serial port commands is shown below, with each element separated by a space:

        ```
        Command ＋ Option ＋ Parameter，for example: wifi_config -c 1
        ```

    6. Serial port commands support line breaks: `\n` and `\r\n`.
    7. Serial port returns execution results at a baud rate of 115200.

### Wi-Fi Command

1. Wi-Fi configuration

    |||Note|
    |-|-|-|
    |Command definition|`wifi_config -i -c <channel (1 ~ 13)> -s <ssid> -b <bssid (xx:xx:xx:xx:xx:xx)> -p <password> -C <country_code>`||
    |Command|wifi_config -i|Get Wi-Fi information|
    ||wifi_config -c -s -b -p -C|Set Wi-Fi configuration|
    |Parameter|`-i` or `--info`|Wi-Fi information|
    |         |`-c` or `--channel`|Wi-Fi work channel|
    |         |`-s` or `--ssid`|AP SSID|
    |         |`-b` or `--bssid`|AP BSSID|
    |         |`-p` or `--password`|AP password|
    |         |`-C` or `--country_code`|Set the current country code|
    |Example|wifi_config -s` "ssid"` -p` "password"`|Wi-Fi sets and connects to the AP with SSID as "ssid" and password as "password".|
    ||wifi_config -c 11|Configure the working channel of the ESP-NOW device to be 11|

2. Scan command

    |||Note|
    |-|-|-|
    |Command definition|`wifi_scan -r <rssi (-120 ~ 0)> -s <ssid> -b <bssid (xx:xx:xx:xx:xx:xx)> -p <time (ms)>`||
    |Command|wifi_scan -r -s -b -p|Scan APs|
    |Parameter|`-r` or `--rssi`|Filter devices by RSSI|
    |         |`-s` or `--ssid`|Filter devices by SSID|
    |         |`-b` or `--bssid`|Filter devices by BSSID|
    |         |`-p` or `--passive`|Passive scan time of each channel|
    |Example|wifi_scan|Scan all APs|
    ||wifi_scan -r -60|Scan devices with RSSI signal value within -60|
    ||wifi_scan -p 600 -b 30:ae:a4:80:16:3c |Scan devices with BSSID 30:ae:a4:80:16:3c and set passive scan time 600 ms every channel|

2. Ping command

    |||Note|
    |-|-|-|
    |Command definition|`ping <host> -W <t> -i <t> -s <n> -c <n> -Q <n>`||
    |Command|`ping host -w -i -s -c -Q`|Send ICMP ECHO_REQUEST to network hosts|
    |Parameter|`host`|Host address|
    |         |`-w` or `--timeout`|Time to wait for a response, in seconds|
    |         |`-i` or `--interval`|Wait interval seconds between sending each packet|
    |         |`-s` or `--size`|Specify the number of data bytes to be sent|
    |         |`-c` or `--count`|Stop after sending count packets|
    |         |`-Q` or `--tos`|Set Type of service related bits in IP datagrams|
    |Example|ping 192.168.0.1|Ping host address 192.168.0.1|

### ESP-NOW Command 
1. Scan

    |||Note|
    |-|-|-|
    |Command definition|`scan <addr (xx:xx:xx:xx:xx:xx)> -a -r <rssi (-120 ~ 0)>`||
    |Command|`scan <addr> -a -r`|Find devices that support ESP-NOW debug|
    |Parameter|`<addr>`|MAC of the monitored device|
    |         |`-a` or `--all`|Full channel scan|
    |         |`-r` or `--rssi`|Filter devices by RSSI|
    |Example|scan -a|Scan all devices in all channel|
    ||scan -r -60|Scan devices with RSSI signal value within -60|
    ||scan 30:ae:a4:80:16:3c |Scan devices with BSSID 30:ae:a4:80:16:3c|

2. Provisioning

    |||Note|
    |-|-|-|
    |Command definition|`provisioning -a -f <wait_ticks> <addrs_list (xx:xx:xx:xx:xx:xx | xx:xx:xx:xx:xx:xx)> <ap_ssid> <app_password>`||
    |Command|provisioning -f|Find devices and get MAC address|
    ||provisioning -a addrs_list ap_ssid ap_password|Configure network for devices|
    |Parameter|`-a` or `--add`|Add device to the network|
    |         |`-f` or `--find`|Find devices waiting to configure the network|
    |         |`addrs_list` `ap_ssid` `ap_password`|Configure network for devices|
    |Example|provisioning -f 3000 -a 30:ae:a4:80:16:3c myssid mypassword|Find devices for 3s and send WiFi configuration to 30:ae:a4:80:16:3c|

3. Control
    |||Note|
    |-|-|-|
    |Command definition|`control  -lasC -b <initiator_attribute> -u <initiator_attribute> -c <initiator_attribute> -m <addr (xx:xx:xx:xx:xx:xx)> -t <responder_attribute> -v <responder_value> -b <count> -t <count> -r <rssi>`||
    |Command|control -lasC -b -u -c -m -t -v -b -t -r|Control devices by esp-now command|
    |Parameter|`-l` or `--list`|Get device binding list|
    |         |`-a` or `--ack`|Wait for the receiving device to return ack|
    |         |`-s` or `--filter_weak_signal`|Discard packets which rssi is lower than forward_rssi|
    |         |`-C` or `--filter_adjacent_channel`|Discard packets from adjacent channels|
    |         |`-b` or `--bind`|Binding with response device|
    |         |`-u` or `--unbind`|Unbinding with response device|
    |         |`-c` or `--command`|Control command to bound device|
    |         |`-m` or `--mac`|MAC of the monitored device|
    |         |`-t` or `--responder_attribute`|Responder's attribute|
    |         |`-v` or `--responder_value`|Responder's value|
    |         |`-b` or `--broadcast`|Broadcast packet|
    |         |`-t` or `--forward_ttl`|Number of hops in data transfer|
    |         |`-r` or `--forward_rssi`|Discard packet which rssi is lower than forward_rssi|
    |Example|control -b 0x200|Bind device, initiator_attribute: 512|
    ||control -u 0x200|Unbind device, initiator_attribute: 512|
    ||control -c 0x200 -t 0x1 -v 0|Send control command,initiator_attribute: 512, set responder_attribute: 1, responder_value: 0|
    ||control -c 0x200 -t 0x101 -v 100|Send control command,initiator_attribute: 512, set responder_attribute: 257, responder_value: 100|

4. OTA

    |||Note|
    |-|-|-|
    |Command definition|`ota -d <url> -f <wait_tick> -s <xx:xx:xx:xx:xx:xx>,<xx:xx:xx:xx:xx:xx>`||
    |Command|ota -d -f -s |Firmware update|
    |Parameter|`-d` or `--download`|Firmware Download url|
    |         |`-f` or `--find`|Find upgradeable devices|
    |         |`-s` or `--send`|Send firmware to selected device|
    |Example|ota -d http://192.168.0.3:8070/hello-world.bin|Download Firmware|
    ||ota -f 3000|Find upgradeable devices for no more than 3s|
    ||ota -s 30:ae:a4:80:16:3c|Send firmware to 30:ae:a4:80:16:3c|

5. Configuration

    |||Note|
    |-|-|-|
    |Command definition|`espnow_config -i -c <channel (1 ~ 13)> -r <rate (wifi_phy_rate_t)> -t <tx_power (8, 84)> -p <protocol_bitmap[1, 2, 4, 8]> -C <country_code ('CN', 'JP, 'US')>`||
    |Command|espnow_config -i -c -r -t -p -C|ESP-NOW configuration|
    |Parameter|`-i` or `--info`|Print all configuration information|
    |         |`-c` or `--channel`|Channel of ESP-NOW|
    |         |`-r` or `--rate`|Wi-Fi PHY rate encodings|
    |         |`-t` or `--tx_power`|Set maximum transmitting power after WiFi start|
    |         |`-p` or `--protocol`|Set protocol type of specified interface|
    |         |`-C` or `--country_code`|Set the current country code|
    |Example|espnow_config -i||
    ||espnow_config -r 0x0B|Config ESPNOW data rate 6 Mbps|

6. Iperf

    |||Note|
    |-|-|-|
    |Command definition|`espnow_iperf -spga -c <responder (xx:xx:xx:xx:xx:xx)> -i <interval (sec)> -l <len (Bytes)> -t <time (sec)> -b <count>`||
    |Command|espnow_iperf -spga -c -i -l -t -b|ESP-NOW iperf|
    |Parameter|`-s` or `--responder`|Run in responder mode, receive from throughput or ping|
    |         |`-p` or `--ping`|Run in ping mode, send to responder|
    |         |`-g` or `--group`|Send a package to a group|
    |         |`-a` or `--abort`|Abort running espnow-iperf|
    |         |`-c` or `--initiator`|Run in initiator mode, ping to <responder>|
    |         |`-i` or `--interval`|Seconds between periodic bandwidth reports (default 3 secs)|
    |         |`-l` or `--len`|Length of buffer in bytes to read or write (Defaults: 230 Bytes)|
    |         |`-t` or `--time`|Time in seconds to transmit for (default 10 secs)|
    |         |`-b` or `--roadcast`|Send package by broadcast|
    |Example|espnow_iperf -s|Run in responder mode|
    ||espnow_iperf -c 30:ae:a4:80:16:3c|Send iperf packages to responder mac|

### *Command* Command 

1. Command

    |||Note|
    |-|-|-|
    |Command definition|`command  -a <addr_list (xx:xx:xx:xx:xx:xx,xx:xx:xx:xx:xx:xx)> <"Command">`||
    |Command|command addr_list "Command"|Let the console command run on the monitoring device|
    |Parameter|`-a` or `--channel_all`|Send packets on all channels|
    |         |`addr_list`|Device MAC address|
    |         |`"Command"`|The command to be executed on a specific device|
    |Example|command 30:ae:a4:80:16:3c "help"|Run help command on device 30:ae:a4:80:16:3c|

### Other Command

* `help`: prints all currently supported commands.

### Web Server
- Open Web server under `Example Configuration` Options:
   * Set `Enable WebServer debugging` to `y`,default is `n`.
   * Set `mDNS Host Name`, default is `espnow-webserver`
   * Set `Wi-Fi SoftAP SSID`, default is `espnow-webserver`
   * Set `Website mount point in VFS`, default is `/www`
- Connect PC or phone to `Wi-Fi SoftAP`
- Open url `http://espnow-webserver` in web browser, and customer can debug devices through the web browser, like getting all devices log and status, controlling or upgrading the monitored devices.

<img src="../../docs/_static/zh_CN/web_server.png" width="1000">

## Note

### Impact on Performance

Since ESP-NOW uses a Wi-Fi interface to send and receive data packages, delay may occur in receiving commands or in data transmission if there is a large amount of data to be transmitted among ESP-NOW devices.

By testing in a good network, we provide the following thresholds, and with such configuration parameters, the delay caused to devices is negligible.

* Fifty ESP-NOW devices (The more the devices, the worse the network.)
* Set logging level to `info` (The lower the level, the worse the network may be.)
