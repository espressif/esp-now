# Solution Example

This example provides the solution to use `ESP-NOW` multiple features in one project.

- Provision WiFi on initiator device through APP and then configure responders WiFi network by espnow provisioning.
- Control lights on responder devices through button on initiator device.
- Debug the responder devices through commands and get log from responder devices.
- Upgrade responder devices through ESP-NOW.
- Security handshake and communication between initiator device and responder devices.

## Hardware Required

This example can run on any ESP32 series boards and at least two development boards are required.

A button needs to be connected to a GPIO pin to configure WiFi network.

A `RGB LED` is needed if the development board doesn't have the RGB LED component. The light is used to indicate WiFi status and control status.

The GPIO pins that drive the button or the light can be configured and modified in [app_main.c](main/app_main.c).

## Configuration

Open the project configuration menu (`idf.py menuconfig`) to set following configurations under `Example Configuration` Options:

* Set `ESP-NOW Mode` of example to be `Initiator Mode` or `Responder Mode`
* Set `Enable ESPNOW Control` to enable ESP-NOW `control` function
* Set `Enable ESPNOW Debug` to enable ESP-NOW `debug` function
* Set `Enable ESPNOW OTA` to enable ESP-NOW `OTA` function
* Set `Enable WiFi Provision over ESP-NOW` to enable ESP-NOW `provision` function
* Set `Enable ESPNOW Security` to enable ESP-NOW `security` function
  - Set `Proof  Possession` string to authorize session.
* Under `WIFI Provision Configuration`, set `BLE` or `SoftAP` provisioning.

> Note that, the initiator/responder board will enable some functions as default. Only the initiator board can enable `WiFi Provision` function, and enabled by default.

## How to Use the Example

Before program the firmware to flash, please firstly use `erase_flash` to erase the entire flash memory.

```shell
idf.py erase_flash flash
```

The following steps will show the default functions when using default configurations. If disable some functions, the steps will be different.

### Step 1: Build & Flash & Run responder boards

When build the project, you can change the firmware name by:

```
cd examples/solution/
rm -rf build/
export PROJECT_NAME=Resp
idf.py build
```

You can find the firmware in `build/Resp.bin`.

If the device runs for the first time, it doesn't have the APP key and can't encrypt/decrypt ESP-NOW data until getting key from initiator board. If the device has stored the APP key, it will read APP key from flash and use the key to encrypt/decrypt ESP-NOW data.

### Step 2: Build & Flash & Run initiator board

If `PROJECT_NAME` is not defined, the default firmware name is `Init.bin`. If you want to change the firmware name, please refer to Step 1.

When initiator board runs, it will start security handshake with responders.

If the initiator board runs for the first time, it will generate a random APP key and store it to flash. Or if it has stored the APP key, it will get the APP key from flash.

After handshake is success, responders will get APP key and store the key to flash.

Then the initiator and responders can communicate ESP-NOW application data in security mode.

### Step 3: WiFi Provision on initiator board

If the device runs for the first time, it has not stored the router info and will not connect to AP. If it has stored the AP info, it will connect to router when WiFi start.

Double click the provision button if the device is not configurated and the light will turn `white`.

Use APP to scan the QR code to provision the initiator board, please refer to [README](https://github.com/espressif/esp-idf/blob/v4.4.1/examples/provisioning/wifi_prov_mgr/README.md)

After initiator board has connected to router, the light will turn `green` , or turn `red` if connection failed. If connecting router succeeds, it will start 30s provision beacon broadcast.

If you want to erase the WiFi config info, please long press the provision button, it will restore WiFi config and restart the board.

### Step 4: WiFi Provision on responder board

Single click the provision button on initiator board, it will also start 30s provision beacon broadcast.

Double click the provision button on responder board, it will enter ESP-NOW provision status and the light will turn `white`. After connecting router succeeds, the light will turn `green` or turn `red` if connection failed.

If you want to erase the WiFi config info, please long press the provision button on responder board, it will restore WiFi config and restart the board.

### Step 5: Control responder boards

Use `Boot button` on initiator board to bind, control and unbind the responder light. Please refer to [README](../control/README.md)

### Step 6: Debug responder boards

The initiator board can debug responder boards through `Command` command running on other devices, and receives running log from responder devices. Please refer to [README](../wireless_debug/README.md)

The responder board will store log in flash and send through ESP-NOW data. It also implements the function of transferring log data to the TCP server on the HTTP network(Default closed. Open the function in `menuconfig`->`Example Configuration`).

### Step 7: OTA through Commands

The initiator board can download new firmware from HTTP server and sends the firmware to responder devices through ESP-NOW. Please refer to OTA command in [README](../wireless_debug/README.md).

## Example Output

Note that the output, in particular the order of the output, may vary depending on the environment.

Console output of the initiator:

```
I (669) wifi:Set ps type: 0

I (672) phy_init: phy_version 909,156dee4,Apr  7 2022,20:27:09
I (757) wifi:mode : sta (7c:df:a1:76:42:38)
I (757) wifi:enable tsf
I (758) ESPNOW: espnow [version: 1.0] init
I (759) phy: chan:1,max_power:80
I (760) phy: chan:2,max_power:80
... ...
I (807) phy: chan:14,max_power:80
I (812) gpio: GPIO[2]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0 
I (821) gpio: GPIO[9]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0 
I (843) espnow_console: Partition size: total: 52961, used: 0
I (845) espnow_console: Command history enabled
esp32c3> W (3333) init: espnow wait security num: 1
I (3333) espnow_sec_init: count: 0, Secure_initiator_send, requested_num: 1, unfinished_num: 1, successed_num: 0
I (4096) init: App key is sent to the device to complete, Spend time: 3265ms, Scan time: 2502ms
I (4097) init: Devices security completed, successed_num: 1, unfinished_num: 0
[7c:df:a1:86:d8:24][1][-20]: I (12854) espnow_sec_resp: Get APP key
I (5566) restart_func: num: 12, reason: 1, crash: 1
esp32c3> 
esp32c3> I (148161) app: WiFi provisioning press
W (148161) BTDM_INIT: esp_bt_mem_release not implemented, return OK
I (148162) wifi_prov_scheme_ble: BT memory released
I (148173) prov: Starting provisioning
I (148174) wifi:Set ps type: 1

W (148175) BTDM_INIT: esp_bt_controller_mem_release not implemented, return OK
I (148186) BTDM_INIT: BT controller compile version [d913766]
I (148198) BTDM_INIT: Bluetooth MAC: 7c:df:a1:76:42:3a

I (148202) wifi_prov_mgr: Provisioning started with service name : PROV_764238 
I (148203) prov: Scan this QR code from the provisioning application for Provisioning.
I (148214) QRCODE: Encoding below text with ECC LVL 0 & QR Code Version 10
I (148225) QRCODE: {"ver":"v1","name":"PROV_764238","pop":"abcd1234","transport":"ble"}
                                    
  █▀▀▀▀▀█ ▀▀▀█▄█   ▀▀▄ ▀▄ █ █▀▀▀▀▀█   
  █ ███ █  ▀▄█ █▄ ▀▄█ ▄██▀  █ ███ █   
  █ ▀▀▀ █  ▄▀█▀▄▀ ▀█▄▀ ██▄▀ █ ▀▀▀ █   
  ▀▀▀▀▀▀▀ █▄▀ █▄█▄█ ▀ █ ▀▄▀ ▀▀▀▀▀▀▀   
  ▀█▄▀▀ ▀▄▄▀▄ ▀▄ ▄▀▀▀█  ▀▄▄ ▀▄▄ ▄▄▀   
   ▄▄▄▄█▀▄█▀  ▀▀▀▀▄▄█    ▀ █  ▄█▄█▀   
   █▄▄▀▀▀█▄▀█ ▄▄██▄ ▀▀▀▄█  ▄▀█ ▀▄▄▀   
  █ ▄▀█▀▀█▄▄  ▄ ▄█▄▀▀█▄▀█▄▀▄▄█  ▄   
  █▄▀▀█▀▀▄▄ ▄█▀▀ █▀▄▀▄▀ ▄█  ███▄ ██   
  ▄ ██▀█▀▀  ▄▄▀▄███▀▄▀█ ▀█ █▀▀ ▀▄▄▀   
  ██ █  ▀ ██ ▀▄▄█▄▀▀█▄█▄█▀▀█ ▀▄ ▄▀  
  █ ▀▄ ▄▀██▀ █▄  ▀█▄█▄▀▀█▀█ ▄█ ▀▄▄█   
  ▀▀▀▀▀▀▀▀▄  ▀▀▄▄██▄█▀█ ▀██▀▀▀█▄▄▀  
  █▀▀▀▀▀█   ██▀▀▀██ ▄▀▄ █▄█ ▀ █ ▄ ▄   
  █ ███ █ █▀▀▄█▀▀█▀▄█▄▄ ▀██▀▀▀▀▄▄▀▀   
  █ ▀▀▀ █ ▄ ▀▀ ▄█▀█ █▀ ▀▀███▄▀█ █▄█   
  ▀▀▀▀▀▀▀ ▀ ▀▀  ▀▀ ▀     ▀▀▀▀▀▀     
                                    

I (148421) prov: If QR code is not visible, copy paste the below URL in a browser.
https://espressif.github.io/esp-jumpstart/qrcode.html?data={"ver":"v1","name":"PROV_764238","pop":"abcd1234","transport":"ble"}
I (148203) protocomm_nimble: BLE Host Task Started
I (148447) NimBLE: GAP procedure initiated: stop advertising.

I (148459) NimBLE: GAP procedure initiated: advertise; 
I (148460) NimBLE: disc_mode=2
I (148470) NimBLE:  adv_channel_map=0 own_addr_type=0 adv_filter_policy=0 adv_itvl_min=256 adv_itvl_max=256
I (148471) NimBLE: 

I (148203) prov: Provisioning started
I (199375) protocomm_nimble: mtu update event; conn_handle=1 cid=4 mtu=256

I (226354) prov: Received Wi-Fi credentials
        SSID     : myssid
        Password : mypassword
I (228322) wifi:new:<5,1>, old:<1,0>, ap:<255,255>, sta:<5,1>, prof:1
I (228971) wifi:state: init -> auth (b0)
I (228993) wifi:state: auth -> assoc (0)
I (229009) wifi:state: assoc -> run (10)
I (229031) wifi:connected with myssid, aid = 7, channel 5, 40U, bssid = e8:9f:80:da:ab:8f
I (229032) wifi:security: WPA2-PSK, phy: bgn, rssi: -64
I (229046) wifi:pm start, type: 1

I (229047) wifi:set rx beacon pti, rx_bcn_pti: 14, bcn_timeout: 14, mt_pti: 25000, mt_time: 10000
I (229052) wifi:BcnInt:102400, DTIM:2
W (229063) wifi:<ba-add>idx:0 (ifx:0, e8:9f:80:da:ab:8f), tid:0, ssn:0, winSize:64
I (230563) prov: Connected with IP Address:192.168.0.42
I (230564) esp_netif_handlers: sta ip: 192.168.0.42, mask: 255.255.255.0, gw: 192.168.0.1
I (230575) init: got ip:192.168.0.42
I (230576) wifi_prov_mgr: STA Got IP
I (230577) prov: Provisioning successful
I (230577) espnow_prov: Responder beacon start, timer: 30s
I (232598) NimBLE: GAP procedure initiated: stop advertising.

I (232603) NimBLE: GAP procedure initiated: stop advertising.

I (232604) NimBLE: GAP procedure initiated: terminate connection; conn_handle=1 hci_reason=19

E (232676) protocomm_nimble: Error setting advertisement data; rc = 30
W (232679) Timer: Timer not stopped
W (232680) Timer: Timer not stopped
I (232685) wifi_prov_mgr: Provisioning stopped
W (232686) BTDM_INIT: esp_bt_mem_release not implemented, return OK
I (232697) wifi_prov_scheme_ble: BTDM memory released
I (232708) wifi:Set ps type: 0

I (260677) espnow_prov: Responder beacon end
I (273601) app: ESPNOW provisioning press
I (273601) espnow_prov: Responder beacon start, timer: 30s
I (274203) init: MAC: 7c:df:a1:86:d8:24, Channel: 5, RSSI: -22, Product_id: initiator_test, Device Name: , Auth Mode: 0, device_secret: 
[7c:df:a1:86:d8:24][5][-23]: I (282958) resp: MAC: 7c:df:a1:76:42:38, Channel: 5, RSSI: -25, Product_id: responder_test, Device Name: 
[7c:df:a1:86:d8:24][5][-29]: I (283485) wifi:[7c:df:a1:86:d8:24][5][-30]: new:<5,1>, old:<5,0>, ap:<255,255>, sta:<5,1>, prof:1[7c:df:a1:86:d8:24][5][-29]: 
[7c:df:a1:86:d8:24][5][-27]: I (284082) wifi:[7c:df:a1:86:d8:24][5][-27]: state: init -> auth (b0)[7c:df:a1:86:d8:24][5][-24]: 
[7c:df:a1:86:d8:24][5][-28]: I (284087) wifi:[7c:df:a1:86:d8:24][5][-31]: state: auth -> assoc (0)[7c:df:a1:86:d8:24][5][-35]: 
[7c:df:a1:86:d8:24][5][-36]: I (284114) wifi:[7c:df:a1:86:d8:24][5][-32]: state: assoc -> run (10)[7c:df:a1:86:d8:24][5][-31]: 
[7c:df:a1:86:d8:24][5][-29]: I (284293) wifi:[7c:df:a1:86:d8:24][5][-30]: connected with myssid, aid = 8, channel 5, 40U, bssid = e8:9f:80:da:ab:8f[7c:df:a1:86:d8:24][5][-30]: 
[7c:df:a1:86:d8:24][5][-31]: I (284295) wifi:[7c:df:a1:86:d8:24][5][-31]: security: WPA2-PSK, phy: bgn, rssi: -68[7c:df:a1:86:d8:24][5][-34]: 
[7c:df:a1:86:d8:24][5][-36]: I (284306) wifi:[7c:df:a1:86:d8:24][5][-38]: pm start, type: 0
[7c:df:a1:86:d8:24][5][-40]: 
[7c:df:a1:86:d8:24][5][-44]: I (284307) wifi:[7c:df:a1:86:d8:24][5][-42]: set rx beacon pti, rx_bcn_pti: 14, bcn_timeout: 14, mt_pti: 25000, mt_time: 10000[7c:df:a1:86:d8:24][5][-37]: 
[7c:df:a1:86:d8:24][5][-37]: I (284308) wifi:[7c:df:a1:86:d8:24][5][-37]: BcnInt:102400, DTIM:2[7c:df:a1:86:d8:24][5][-37]: 
[7c:df:a1:86:d8:24][5][-37]: W (284533) wifi:[7c:df:a1:86:d8:24][5][-37]: <ba-add>idx:0 (ifx:0, e8:9f:80:da:ab:8f), tid:0, ssn:0, winSize:64[7c:df:a1:86:d8:24][5][-35]: 
[7c:df:a1:86:d8:24][5][-33]: I (285584) esp_netif_handlers: sta ip: 192.168.0.45, mask: 255.255.255.0, gw: 192.168.0.1
[7c:df:a1:86:d8:24][5][-35]: I (285585) resp: got ip:192.168.0.45
esp32c3> 
esp32c3> 
esp32c3> I (303701) espnow_prov: Responder beacon end
I (307341) app: WiFi provisioning press
I (331541) app: initiator bind press
[7c:df:a1:86:d8:24][5][-22]: I (340306) espnow_ctrl: bind, esp_log_timestamp: 340306, timestamp: 30802, rssi: -25, rssi: -55
[7c:df:a1:86:d8:24][5][-22]: I (340307) control_func: addr: 7c:df:a1:76:42:38, initiator_type: 2, initiator_value: 1
[7c:df:a1:86:d8:24][5][-23]: I (340319) app: bind, uuid: 7c:df:a1:76:42:38, initiator_type: 513
I (336321) app: initiator send press
[7c:df:a1:86:d8:24][5][-20]: I (345086) app: espnow_ctrl_responder_recv, initiator_attribute: 513, responder_attribute: 1, value: 0
I (338166) app: initiator send press
[7c:df:a1:86:d8:24][5][-19]: I (346931) app: espnow_ctrl_responder_recv, initiator_attribute: 513, responder_attribute: 1, value: 1
... ...
I (346686) app: initiator unbind press
[7c:df:a1:86:d8:24][5][-20]: I (355451) app: unbind, uuid: 7c:df:a1:76:42:38, initiator_type: 513

```

Console output of the responder:

```I

I (653) phy_init: phy_version 909,156dee4,Apr  7 2022,20:27:09
I (738) wifi:mode : sta (7c:df:a1:86:d8:24)
I (738) wifi:enable tsf
I (739) ESPNOW: espnow [version: 1.0] init
I (740) phy: chan:1,max_power:80
I (741) phy: chan:2,max_power:80
... ...
I (788) phy: chan:14,max_power:80
I (793) gpio: GPIO[2]| InputEn: 1| OutputEn: 0| OpenDrain: 0| Pullup: 1| Pulldown: 0| Intr:0 
I (802) esp_timesync: Initializing SNTP. Using the SNTP server: pool.ntp.org
I (827) espnow_log_flash: LOG flash initialized successfully
I (828) espnow_log_flash: Log save partition subtype: label: log_info, addr:0x3d0000, offset: 0, size: 65536
I (839) espnow_log: log initialized successfully
esp32c3> I (5542) restart_func: num: 3, reason: 1, crash: 1
I (12854) espnow_sec_resp: Get APP key
esp32c3> 
esp32c3> I (269307) app: WiFi provisioning press
I (269310) phy: chan:1,max_power:80
I (269310) phy: chan:2,max_power:80
... ...
I (269358) phy: chan:14,max_power:80
I (282958) resp: MAC: 7c:df:a1:76:42:38, Channel: 5, RSSI: -25, Product_id: responder_test, Device Name: 
I (282994) resp: MAC: 7c:df:a1:76:42:38, Channel: 5, RSSI: -25, wifi_mode: 0, ssid: myssid, password: mypassword, token: 
I (283064) resp: provisioning initiator exit
I (283485) wifi:new:<5,1>, old:<5,0>, ap:<255,255>, sta:<5,1>, prof:1
I (284082) wifi:state: init -> auth (b0)
I (284087) wifi:state: auth -> assoc (0)
I (284114) wifi:state: assoc -> run (10)
I (284293) wifi:connected with myssid, aid = 8, channel 5, 40U, bssid = e8:9f:80:da:ab:8f
I (284295) wifi:security: WPA2-PSK, phy: bgn, rssi: -68
I (284306) wifi:pm start, type: 0

I (284307) wifi:set rx beacon pti, rx_bcn_pti: 14, bcn_timeout: 14, mt_pti: 25000, mt_time: 10000
I (284308) wifi:BcnInt:102400, DTIM:2
W (284533) wifi:<ba-add>idx:0 (ifx:0, e8:9f:80:da:ab:8f), tid:0, ssn:0, winSize:64
I (285584) esp_netif_handlers: sta ip: 192.168.0.45, mask: 255.255.255.0, gw: 192.168.0.1
I (285585) resp: got ip:192.168.0.45
esp32c3> 
esp32c3> 
esp32c3> I (340306) espnow_ctrl: bind, esp_log_timestamp: 340306, timestamp: 30802, rssi: -25, rssi: -55
I (340307) control_func: addr: 7c:df:a1:76:42:38, initiator_type: 2, initiator_value: 1
I (340319) app: bind, uuid: 7c:df:a1:76:42:38, initiator_type: 513
I (345086) app: espnow_ctrl_responder_recv, initiator_attribute: 513, responder_attribute: 1, value: 0
I (346931) app: espnow_ctrl_responder_recv, initiator_attribute: 513, responder_attribute: 1, value: 1
... ...
I (355451) app: unbind, uuid: 7c:df:a1:76:42:38, initiator_type: 513
```

## Troubleshooting

* Errors `E (14798) espnow_sec: Failed at mbedtls_ccm_auth_decrypt with error code : -15`. It means authentication failed. Maybe initiator board erases its flash and generates a new security key, not the same with responder. If you see this error please erase responder board flash and reset responder board, the security handshake will execute again after reset initiator board. The responder board will get the security key to keep the same with initiator board.
* Build with ESP32-C3 failed with 'ADC_BUTTON_WIDTH' undeclared.
  Please change the defination as the following in [button_adc.c](managed_components/espressif__button/button_adc.c)

```
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32C3
#define ADC_BUTTON_WIDTH       ADC_WIDTH_BIT_12
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_BUTTON_WIDTH       ADC_WIDTH_BIT_13
#endif

```
