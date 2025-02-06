1. Introduction
===============

The `ESP-NOW <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html>`__ provided by `ESP-IDF <https://github.com/espressif/esp-idf>`__ is the ESP-NOW protocol, this component provides some high-level functionalities to simplify the use of ESP-NOW protocol, you can understand this component as an application-level ESP-NOW, which provides some enhanced features, including pairing, control, provisioning, debug, OTA, security, etc.

ESP-NOW occupies less CPU and flash resource. It can work with Wi-Fi and Bluetooth LE, and supports the series of ESP8266、ESP32、ESP32-S and ESP32-C. The data transmission mode of ESP-NOW is flexible including unicast and broadcast, and supports one-to-many and many-to-many device connection and control.

.. figure:: ../_static/en/function_list.png
    :align: center
    :alt: ESP-NOW Function List
    :figclass: align-center

There are two roles defined in ESP-NOW according to the data flow, initiator and responder. The same device can have two roles at the same time. Generally, switches, sensors, LCD screens, etc. play the role of initiator in an IoT system, when lights, sockets and other smart applications play the role of responder.

.. figure:: ../_static/en/device_role.png
    :align: center
    :alt: ESP-NOW Device Roles
    :figclass: align-center
