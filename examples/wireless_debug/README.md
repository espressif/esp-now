# ESP-NOW Debug Receiver Board Demo

ESP-NOW debug receiver board can receive debugging data from devices only when the board is on the same Wi-Fi channel with the devices.

> Note:
> 1. If the ESP-NOW debug receiver board is on the same channel with devices, you don't need to connect the borad with the router.
> 2. The following code needs to be added to the monitored ESP-NOW device:
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

ESP-NOW debug receiver board provides the following features:

 - [SD card file management](#Commands-for-SD-Card-Files): lists all files in current SD card, deletes specific files, and prints file contents optionally in some formats such as hex, string or base64.
 - [Sniffer listening for surrounding IEEE 802.11 packages](#Sniffer-Command): captures packages and saves them in pcap format in SD card, specifies the name of the file to save captured packages, sets package filters, and specifies a channel to listen for.
 - [Wi-Fi configuration](#Wi-Fi-Command): sets Wi-Fi information needed in STA mode, including router SSID, password, BSSID and work channel, and saves/erases the configuration information. 
 - [Wi-Fi scan](#Scan-Command): works in STA mode, and scans AP or ESP-NOW devices nearby, sets filters such as filtered by RSSI, SSID or BSSID, and sets passive scan time in each channel. 
 - [Log configuration](#Log-Command): monitors logs from other devices, counts the numbers of various logs (I, W, E), restart times and coredump times and displays them on the screen, adds/removes monitors, and sets logging level.
 - [Command](#Command-<"command">): runs commands on specific devices.
 - [General command](#Other-Command): includes help command to print all currently supported commands.
## ESP-NOW Introduction

### Overview

ESP-NOW debug receiver board receives running log and coredump data from ESP-NOW devices via [ESP-NOW](https://esp-idf.readthedocs.io/en/latest/api-reference/wifi/esp_now.html) wireless transmission technology. ESP-NOW is a connectionless Wi-Fi protocol defined by Espressif, widely used in smart lighting, remote control, sensors and other fields. In ESP-NOW, data is encapsulated in Wi-Fi Action frame_head for data transmission, transferring data from one Wi-Fi device to another without connection.

### ESP-NOW Features

1. The sender and the receiver must be on the same channel.
2. The receiver may not add the MAC address of the sender in the case of non-encrypted communication (MAC address is needed in encrypted communication), but the sender must add the MAC address of the receiver.
3. ESP-NOW can add up to 20 paired devices, and supports six of these devices to encrypt communications.
4. Register callback functions to receive data packages, and check the delivery status (succeed or fail).
5. Ensure data security by CTR and CCBC-MAC protocol (CCMP).

> For more information about ESP-NOW, please refer to [ESP-NOW Instructions](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/hw-reference/esp32/get-started-wrover-kit-v2.html) and ESP-IDF example [espnow](https://github.com/espressif/esp-idf/tree/master/examples/wifi/espnow)

## ESP-NOW Debug Demo Instructions
### Project Structure

### Workflow

1. Compile and flash this project to an ESP32 development board;
2. Open serial port terminal and restart development board:
> Please use serial port terminals such as `minicom` to avoid some unexpected problems when using `idf.py monitor`.

3. Debug ESP-NOW by entering the following commands according to the prompts.
> The following describes the use of each command in sequence.

### Serial Port Commands

* ESP-NOW debug receiver board supports the following serial port commands: help, sdcard, wifi_sniffer, wifi_config, wifi_scan, log, coredump and command.

* The interaction of serial port commands follows the following rules:
    1. PC sends commands to ESP-NOW debug receiver board through serial port with a baud rate of 115200.
    2. In command definition, all characters are lowercase (some options are uppercase), and strings do not need to be quoted.
    3. The elements in brackets {} in command description should be taken as a parameter and be replaced as the case may be.
    4. The part contained in square brackets [] in command description is the default value and can be filled in or displayed.
    5. The pattern of serial port commands is shown below, with each element separated by a space:

        ```
        Command ＋ Option ＋ Parameter，for example: wifi_config -c 1
        ```

    6. Serial port commands support line breaks: `\n` and `\r\n`.
    7. Serial port returns execution results at a baud rate of 115200.

### Commands for SD Card Files

1. List files

    |||Note|
    |-|-|-|
    |Command definition|sdcard -l <file_name>||
    |Command|sdcard -l|Display a list of all matched files in SD card |
    |Parameter|file_name|String to be matched<br>`* `means to display all files<br>`*.ab ` means to display all ab files 
    |Example|sdcard -l *.pcap|Display all pcap files in SD card|

2. Remove files

    |||Note|
    |-|-|-|
    |Command definition|sdcard -r <file_name>||
    |Command|sdcard -r|Remove specific files or all matched files|
    |Parameter|file_name|File name or string to be matched<br>`*` means to remove all files<br>`*.ab ` means to remove all ab files<br> `a.abc` means to remove a.abc files|
    |Example|sdcard -r *.pcap|Remove all pcap files in SD card|

3. Print file content

     |||Note|
    |-|-|-|
    |Command definition|sdcard -o <file_name> -t <type>||
    |Command|sdcard -o|Print the content of a specific file|
    |Parameter|file_name|File name<br> `a.abc` means to print the content of a.abc file |
    ||type|File print type<br>`hex `means to print files in hex <br>`string` means to print files in string<br>`base64` means to print files in base64 |
    |Example|sdcard -o a.abc|Print the content of a.abc file|

### Sniffer Command

1. Configure sniffer monitoring channel

    |||Note|
    |-|-|-|
    |Command definition|wifi_sniffer -c <channel (1 ~ 13)>||
    |Command|wifi_sniffer -c|Configure sniffer monitoring channel|
    |Parameter|channel|Channel number|
    |Example|wifi_sniffer -c 11|Sniffer monitors channel 11|

2. Configure the name of the file used to save monitored data packages

   |||Note|
    |-|-|-|
    |Command definition|wifi_sniffer -f <file_name> ||
    |Command|wifi_sniffer -f|File name used to save monitored packages|
    |Parameter|file_name|File name<br>`sniffer.pcap ` means to save the data to sniffer.pcap file |
    |Example|wifi_sniffer -f sniffer.pcap|The data monitored by sniffer is saved in sniffer.pcap|

3. Configure filters for data packages monitoring

    |||Note|
    |-|-|-|
    |Command definition|wifi_sniffer -F <mgmt\|data\|ctrl\|misc\|mpdu\|ampdu> ||
    |Command|wifi_sniffer -F|Set filters for data package|
    |Parameter|mgmt\|data\|ctrl\|misc\|mpdu\|ampdu|Filter types<br>`ampdu`  means to filter ampdu data packages|
    |Example|wifi_sniffer -F ampdu|Sniffer monitors and filters ampdu data packages|

4. Stop sniffer monitoring

    |||Note|
    |-|-|-|
    |Command definition|wifi_sniffer -s||
    |Command|wifi_sniffer -s|Stop monitoring|
    |Example|wifi_sniffer -s|Stop sniffer monitoring|

### Wi-Fi Command

1. Wi-Fi configuration

    |||Note|
    |-|-|-|
    |Command definition|wifi_config -c <channel (1 ~ 13)> -s <ssid> -b <bssid (xx:xx:xx:xx:xx:xx)> -p <password>||
    |Command|wifi_config -c -s -b -p|Wi-Fi configuration|
    |Parameter|channel|Wi-Fi work channel|
    ||ssid|AP SSID|
    ||bssid|AP BSSID|
    ||password|AP password|
    |Example|wifi_config -s` "ssid"` -p` "password"`|Wi-Fi sets and connects to the AP with SSID as "ssid" and password as "password".|
    ||wifi_config -c 11|Configure the working channel of the ESP-NOW device to be 11|

2. Scan conmmand

    |||Note|
    |-|-|-|
    |Command definition|wifi_scan -r <rssi (-120 ~ 0)> -s <ssid> -b <bssid (xx:xx:xx:xx:xx:xx)> -p <time (ms)>||
    |Parameter|rssi|Filter devices by RSSI|
    ||ssid|Filter devices by SSID|
    ||bssid|Filter devices by BSSID|
    ||passive|Passive scan time of each channel|
    |Example|wifi_scan|Scan all APs|
    ||wifi_scan -r -60|Scan devices with RSSI signal value within -60|
    ||wifi_scan -m -p 600 -i 30:ae:a4:80:16:3c |Scan mesh devices with mesh_id 30:ae:a4:80:16:3c|

### Log Command

1. Log Configuration

    |||Note|
    |-|-|-|
    |Command definition|log  [-ari] <mac (xx:xx:xx:xx:xx:xx)> [-t <tag>] [-l <level>]||
    |Command|log -a|Add a log monitor|
    ||log -r|Remove a log monitor|
    |Parameter|mac|MAC address of the monitor|
    ||tag|Use tag to filter log|
    ||level|Use level to filter log|
    |Command|log -i|Print log data of the monitor|
    |Parameter|mac|MAC address of the monitor|
    |Example|log -i 30:ae:a4:80:16:3c|Print log data of the monitor 30:ae:a4:80:16:3c|
    ||log -a 30:ae:a4:80:16:3c|Add the monitor 30:ae:a4:80:16:3c|
    ||log -r 30:ae:a4:80:16:3c|Remove the monitor 30:ae:a4:80:16:3c|
    ||log 30:ae:a4:80:16:3c -t * -l INFO|Set the level of all log output from monitor 30:ae:a4:80:16:3c to INFO|
### *Command* Command 

1. Command

    |||Note|
    |-|-|-|
    |Command definition|command  <addr ((xx:xx:xx:xx:xx:xx))> <*Command*>||
    |Parameter|addr|Device MAC address|
    ||*Command*|The command to be executed on a specific device|
    |Example|command 30:ae:a4:80:16:3c help|Run help command on device 30:ae:a4:80:16:3c|

### Other Command

* `help`: prints all currently supported commands.

## Impact on Performance

Since ESP-NOW, uses a Wi-Fi interface to send and receive data packages, delay may occur in receiving commands or in data transmission if there is a large amount of data to be transmitted among ESP-NOW devices.

By testing in a good network, we provide the following thresholds, and with such configuration parameters, the delay caused to devices is negligible.

* Fifty ESP-NOW devices (The more the devices, the worse the network.)
* Set logging level to `info` (The lower the level, the worse the network may be.)
