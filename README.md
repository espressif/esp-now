# ESP-NOW (Beta)
This project provides examples to simplify the use of ESP-NOW.  

ESP-NOW is a kind of connectionless Wi-Fi communication protocol that is defined by Espressif. Different from traditional Wi-Fi protocols, the first five upper layers in OSI are simplified to one layer in ESP-NOW, so the data does not need to go through the physical layer, data link layer, network layer, transport layer in turn, which reduces the delay caused by packet loss under congested network, and leads to quickly response time.

<img src="docs/_static/en/protocol_stack.png" width="800">

## Introduction

ESP-NOW occupies less CPU and flash resource. It can work with Wi-Fi and Blutooth LE, and supports the series of ESP8266、ESP32、ESP32-S and ESP32-C. The data transmission mode of ESP-NOW is flexible including unicast and broadcast, and supports one-to-many and many-to-many device connection and control. ESP-NOW can be also used as an independent auxiliary module to help network configuration, debugging and firmware upgrades.

<img src="docs/_static/en/function_list.png" width="800">

There are two roles defined in ESP-NOW according to the data flow, initiator and responder. The same device can have two roles at the same time. Generally, switches, sensors, LCD screens, etc. play the role of initaitor in an IoT system, when lights, sockets and other smart applications play the role of responder.

<img src="docs/_static/en/device_role.png" width="750">

## TODO List
  - [ ] Gateway
  - [ ] Low-power
  - [ ] Frequency-Hopping
  - [ ] ESP8266 supporting

## Quick Start
### Hardware Preparation
Chips of ESP32、ESP32-C3、ESP32-S2 are recommended, ESP8266 and ESP32-S3 will be supported soon.

`examples/dev_kits` includes examples based on specific development boards. You need to buy the related boards before running the examples.

### Set up Development Environment
Setting the environment and getting ESP-IDF (release/v4.3 or tag v4.3.x) follow the [Step](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32/index.html) .

### Get the ESP-NOW project
Download ESP-NOW using the following:
```shell
git clone --recursive https://github.com/espressif/esp-now.git
```
> note: the `--recursive` option. This is required to pull the dependencies into ESP-NOW. In case you have already cloned the repository without this option, execute this to pull the submodules: `git submodule update --init --recursive`

### Build and Flash firmware
It is recommended to first erase the flash if you are using this for the first time and then flash the firmware. Here are the steps:
```shell
$ cd /path/to/esp-now/examples/get-started/
$ export ESPPORT=/dev/tty.SLAB_USBtoUART #
$ idf.py set-target esp32s2
$ idf.py erase_flash
$ idf.py flash monitor
```

## Function
### Control
There are some obvious advantages in ESP-NOW:

1. **Quick Response**: After power-on, the devices can transmit data and control other paired devices directly without any wireless connection, and the response speed is in milliseconds.
2. **Low-power**: ESP-NOW simplifies the five layer protocols into one layer, which leads to easier communication process and lower power consumption. A control button can be used for two years just with two AA batteries.
3. **Good Compatibility**: When the device connects to a router or works as a hotpot, it can also realize a fast and stable communication by ESP-NOW. And the device can keep stable connection through ESP-NOW even if the router is faulty or the network is unstable.
4. **Long-distance Communication**: ESP-NOW supports long-distance communication. It can be applied to outdoor scenes and can keep stable connection even the devices are seperated by walls even floors.
5. **Multilayer Control**: The multi-hop control of devices can be realized by ESP-NOW. Hundreds of devices can be controlled through unicast, broadcast and group control.
6. **Multiple Control Methods**: ESP-NOW supports the touch switch, LCD screen, different sensors and voice control.

### Provision

ESP-NOW provides a new provisioning method besides the Wi-Fi provisioning and Bluetooth provisioning. First, configuring the network for the first device via bluetooth, and other devices don't need to be configured the information of SSID/password, because the first device connected to the network can send these information to others directly. Users can choose whether to allow remaining devices to access the network on the APP side.

### Upgrade
ESP-NOW can be used for the mass data transmission like firmware upgrade.

1. **Resume Upgrade from Break-point**: When use ESP-NOW to upgrade the firmware, the firmware will be subpackaged in a fixed size and be written to the flash one by one, and the device will record upgraded packages. If the upgrade process is interrupted, the device will only request the remaining firmware packages to resume upgrade from break point.
2. **Multiple Devices Upgrade**: ESP-NOW can support multiple devices upgrade at same time. 50 devices can be upgraded in 3 minutes.
3. **Rollback**: The firmware can rollback to previous version if an upgrade error occurs.

### Debug

ESP-NOW can be used to receive the running log for debugging. It can be used in scene where the devices can't be contact directly because of the high-voltage electricity, high temperature. With the supporting of many-to-many connections, the initiator can receive logs from multiple responders to diagnose device faults quickly.

1. Device Log
    - Log Analysis: analysis the running time and restart times from log.
    - Log Storage: store the acquired data in the SD cards or export to the web.
    - Log Level Modification: the log level of each function model can be freely adjusted.
2. Debug Commands
    - Peripheral Debugging: Control commands can be sent to test the peripherals like GPIO, UART, LED, etc.
    - Wi-Fi Debugging: The country code, Wi-Fi mode, Wi-Fi power, etc., can be adjusted, and the performance of Wi-Fi can be debugged.
    - Status Debugging: restart command, reset command, memory and task running status command.
    - Custom Commands
3. Production Test
    - Aging Test: random restart, long-term monitoring of device
    - Interference Test: distribute a large number of Wi-Fi data packets to interfere with the network.
    - Wireless Test: monitor the RF performance, ping packet test, distance and RF performance test.
    - Module Test: flash read time test, time accuracy test.
    - Version Verification: verify whether the factory version of the device is the specified version.

### Data Encryption

ESP-NOW can protect the data security with ECDH and AES128-CCM.

1. **Quick Configuration**: 16 devices can be configurated in 5 seconds.
2. **Multiple Devices Handshake**: ESP-NOW initiator can support multiple devices handshake at same time. 
3. **Safety**:
    - ECDH and Proof of Possession (PoP) string used to authorize session and derive shared key
    - AES256-CTR mode encryption for the configuration data
    - AES128-CCM mode encryption for ESP-NOW data.

## Resources
- [ESP-NOW API guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/network/esp_now.html)
- [ESP-NOW in Arduino](https://github.com/yoursunny/WifiEspNow)
