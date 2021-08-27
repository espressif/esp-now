# ESP-NOW (Beta)
This project provides examples to simplify the use of ESP-NOW.  

ESP-NOW is a kind of connectionless Wi-Fi communication protocol that is defined by Espressif. Different from tradional Wi-Fi protocols, the first five upper layers in OSI are simplified to one layer in ESP-NOW, the data does not need to go through the physical layer, data link layer, network layer,  transport layer in turn, which reduces the delay caused by packet loss under congested network, leads to quickly response time.

<img src="docs/_static/en/protocol_stack.png" width="800">

## Introduce

ESP-NOW occupies less CPU and flash resource, it can work with Wi-Fi and Blutooth LE, supports the series of ESP8266、ESP32、ESP32-S and ESP32-C. The data transmission mode of ESP-NOW is flexible including unicast and broadcast, support one-to-many and many-to-many  device connection and control. And ESP-NOW can be used as an independent auxiliary module to help network configuraresponcetion, debugging and firmware upgrades.

<img src="docs/_static/en/function_list.png" width="800">

There are two roles defined in ESP-NOW according to the data flow, initiator and responder. The same device can have two roles at the same time. Generally,  switches, sensors LCD screens etc. play the role of initaitor in an IoT system, when lights, sockets and other smart applications play the role of responder.

<img src="docs/_static/en/device_role.png" width="750">

## TODO List
  - [ ] Gateway
  - [ ] Low-power
  - [ ] Data Encryption
  - [ ] Frequency-Hopping
  - [ ] ESP8266 supporting

## Quick Start
### Hardware Preparation
Chips of ESP32、ESP32-C3、ESP32-S2 are recommended, ESP8266 and ESP32-S3 will be supported soon.

`examples/dev_kits` includes examples based on specific development boards. You need to buy the related boards before running the examples.

### Set up Development Environment
Setting the environment and getting  ESP-IDF (master) follow the  [Step](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) .

### Get the ESP-NOW project
Download ESP-NOW using the following:
```shell
git clone --recursive https://github.com/espressif/esp-now.git
```
> note: the `--recursive` option. This is required to pull in the dependencies into  ESP-NOW. In case you have already cloned the repository without  this option, execute this to pull in the submodules: `git submodule update --init --recursive`

### Build and Flash firmware
It is recommended to first erase the flash if you are using this  for the first time and then flash the firmware. Here are the steps:
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
2. **Low-power**: ESP-NOW simplifies the five layer protocols into one layer, which leads to easier communication process and lower power consumption,  a  control button can be used for two years just with two AA batteries.
3. **Good Compatibility**: When the device connects to router or works as a hotpot, it can also relize a fast and stable communication by ESP-NOW. And the device can keep stable connection through ESP-NOW even if the router is faulty or the network is unstable.
4. **Long-distance Communication**:  ESP-NOW supports long-distance communication, it can be applied to outdoor scenes and can keep stable connection even the devices are seprated by walls even floors.
5. **Multilayer Control**: We can realize the multi-hop control of devices by ESP-NOW.  Hundreds of devices can be controlled through unicast, broadcast and group control.
6. **Multiple  Control Methods**:  ESP-NOW supports the touch switch, LCD screen, different sensors and voice control.
### Provision

ESP-NOW is a provisioning method besides the Wi-Fi provisioning and  Bluetooth provisioning. First, configure the network for the first device via bluetooth, and other devices don't need the information of SSID/password, the first device connected to the network can send these information to others directly.  Users can choose whether to allow remaining devices to access the network on the APP side.

### Upgrade
ESP-NOW can be used for the mass data transmission like firmware upgrade.

1. **Resume Upgrade from Break-point**: when use ESP-NOW to upgrade the firmware, the firmware will be subpackaged in a fixed size and be wrote to the flash on by one., the device will recorde packages upgraded. If the upgrade process is interrupted, the device will only request the remain firemware packages  to realize resume upgrade from break point.
2. **Multiple Devices Upgrade**: ESP-NOW can support multiple devices upgrade at same time, 50 devices can be upgraded in 3 minutes.
3. **Version Reset**: the firmware can reset to previous version if an upgrade error occurs.

### Debug

ESP-NOW can be used to receive the running log for debugging. It can be used in scene where the devices can't be contact directly because of the high-voltage electricity, high temperature. With the supporting of many-to many connections, the initiator can receive logs from multiple responders to diagnose device faults quickly.

1. Device Log
    - Log Analysis: analysis the running time and restart times from log.
    - Log Storage: store the acquired data in the SD cards or export to the web.
    - Log Modify: the log level of each function modele can be free adjusted.
2. Command Debugging
    - Peripheral Debugging: control commands can be sent to test tje peripherals like GPIO, UART, LED etc.
    - Wi-Fi Debugging: the country code, Wi-Fi mode, Wi-Fi power .etc can be adjusted , and  the perfomance of Wi-Fi can be  debug.
    - Status Debugging: restart command, reset command, momory and task running status.
    - Custom Command
3. Production Test
    - Aging Test: random restart, long-term monitoring of device
    - Interference Test: distribute a large number of Wi-Fi data packets to interfere with the network.
    
    - Wireless Test: monitor the RF performance, ping packet test, distance and RF performance test.
    - Module Test: flash read time test, time accuracy test.
    - Version Verification: verify whether the factory version of the device is the specified version.

## Resources
- [ESP-NOW API guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/network/esp_now.html)
- [ESP-NOW in Arduino](https://github.com/yoursunny/WifiEspNow)
