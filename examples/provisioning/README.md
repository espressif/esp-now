## Provisioning Example

This example demonstrates how to use ESP-NOW provisioning feature to do WiFi provisioning for other devices.

## Hardware Required

This example can run on any ESP32 series development board and at least two development boards are required.

## How to Use the Example

### Configure the devices

Navigate to the provisioning example directory, and type `idf.py menuconfig` to configure the provisioning example.

At least 2 boards are needed. You need to set the corresponding options and to program firmware to the different boards.

For board 1, it will work as the initiator device. Set following configurations under `Example Configuration` Options:
* Set `ESP-NOW provisioning Mode` as `Initiator Mode`

For board 2, it will work as the responder device. Set following configurations under `Example Configuration` Options:
* Set `ESP-NOW provisioning Mode` as `Responder Mode`
* Set `WiFi SSID` of the Router (Access-Point)
* Set `WiFi Password` of the Router (Access-Point)

### Operating procedures

When this example on the initiator device starts up, it will:

1. Keep scanning the provision beacon until receive it.
2. Send device type provision frame to request WiFi credential which resides in the responder device.
3. If receive WiFi type provision frame, get WiFi credential from the frame and connect to the Router.

When this example on the responder device starts up, it will:

1. Broadcast provision beacons every 100 ms in 30 seconds.
2. If receive device type provision frame, response with the WiFi type provision frame which contains WiFi credential.

> Authenticating the device through the information of the initiator is not presented in the example.

## Example Output

Console output of the initiator device:

```
I (575) ESPNOW: espnow [version: 1.0] init
I (339495) app_main: MAC: 7c:df:a1:86:d8:24, Channel: 1, RSSI: -35, Product_id: responder_test, Device Name: 
I (339515) app_main: MAC: 7c:df:a1:86:d8:24, Channel: 1, RSSI: -36, wifi_mode: 0, ssid: myssid, password: , token: 
I (339525) wifi:new:<1,1>, old:<1,0>, ap:<255,255>, sta:<1,1>, prof:1
I (339525) wifi:state: init -> auth (b0)
I (339535) wifi:state: auth -> assoc (0)
I (339545) wifi:state: assoc -> run (10)
I (339545) wifi:connected with myssid, aid = 2, channel 1, 40U, bssid = 3c:46:d8:0e:4c:ac
I (339545) wifi:security: Open Auth, phy: bgn, rssi: -32
I (339555) wifi:pm start, type: 0

I (339555) wifi:set rx beacon pti, rx_bcn_pti: 0, bcn_timeout: 0, mt_pti: 25000, mt_time: 10000
W (339565) wifi:<ba-add>idx:0 (ifx:0, 3c:46:d8:0e:4c:ac), tid:0, ssn:0, winSize:64
I (339605) wifi:BcnInt:102400, DTIM:1
I (340345) esp_netif_handlers: sta ip: 192.168.1.100, mask: 255.255.255.0, gw: 192.168.1.253
```

Console output of the responder device:

```
I (578) ESPNOW: espnow [version: 1.0] init
I (588) espnow_prov: Responder beacon start, timer: 30s
I (688) app_main: MAC: 7c:df:a1:76:42:38, Channel: 1, RSSI: -39, Product_id: initiator_test, Device Name: , Auth Mode: 0, device_secret: 
I (30688) espnow_prov: Responder beacon end
```
