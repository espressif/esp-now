# Debug log package upload example

## Introduction

This example shows how to connect a device to a remote external server based on the `http_client` module APIs. The device acts as the root node to transfer all data to the remote server.

This example implements the function of transferring log data to the TCP server in the device on the http network. The data is packaged into a file and transmitted to server, and uploaded according to the size of the data `ESPNOW_DATA_LEN`. In addition, a `console` control command is added, and the basic information of the device can be obtained through the serial port command.

## Process

### Run TCP server

1. Connect PC or the mobile phone to the router.
2. Use a TCP testing tool (any third-party TCP testing software) to create a TCP server.

### Configure the devices

Navigate to the OTA example directory, and type `idf.py menuconfig` to configure the OTA example. 

Set following parameters under `Example Connection Configuration` Options:
* Set `WiFi SSID` of the Router (Access-Point).
* Set `WiFi Password` of the Router (Access-Point).
* Set `IP version` of the example to be IPV4 or IPV6.
* Set `Port` number that represents remote port the example will create.

Set following parameter under `ESP-NOW Debug Example Configuration` Options:
* Set firmware upgrade URL。
  
```
https://<host-ip-address>:<host-port>

for e.g,
https://192.168.0.3:8070/flash_log
```

### Operating procedures

1. The user needs to open the network assistant tool and connect to the network.
2. The user controls the information through the console command line, and the help can view the format of the input.
* The main serial command line is as follows:
```shell
system_info -i   // This will output basic information
```

### Note

1. The network that requires the phone link here is the same as the esp32 chip, so that data information can be collected on the network assistant tool. 
1. It should also be noted that when using a new chip for burning, the chip needs to be erased and then burned.
1. The log_info area is added to the partition table in this example as a space to save logs. The chip needs to be erased before programming.
1. The head of the data is a timestamp. It is just an experiment and there is no real-time calibration. It can be modified according to the user's own needs.
1. Get the `uart`, `flash`, `espnow` status in the Console command, you can't use the `console` command to disable it, because the main program has been forced to be enabled. If the user wants to use such a function, the main program can be disable.
