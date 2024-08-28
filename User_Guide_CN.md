# ESP-NOW

* [English Version](./User_Guide.md)

本项目提供了一些 ESP-NOW 的使用示例。

[ESP-NOW](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-reference/network/esp_now.html) 是乐鑫定义的一种无连接 Wi-Fi 通信协议。与传统的 Wi-Fi 协议不同，ESP-NOW 将 OSI 模型中的前 5 层简化为了一层，因此数据无需经过网络层、传输层、会话层、表示层和应用层进行传输，减少了网络拥塞下因数据丢包引起的延迟，实现了快速响应。

<img src="docs/_static/en/protocol_stack.png" width="800">

## 概述

[ESP-NOW](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/api-reference/network/esp_now.html)是 [ESP-IDF](https://github.com/espressif/esp-idf) 提供的一个协议，该组件配备了一些高级功能，以简化 ESP-NOW 协议的使用。此组件可以理解为应用级的 ESP-NOW，具有配对、控制、配网、调试、OTA、安全等增强功能。

ESP-NOW 占用较少的 CPU 和 flash 资源。ESP-NOW 可以与 Wi-Fi 和低功耗蓝牙一起工作，并且支持 ESP8266、ESP32、ESP32-S 和 ESP32-C 系列芯片。ESP-NOW 的数据传输模式灵活，包括单播和广播，并支持一对多和多对多设备连接和控制。

<img src="docs/_static/en/function_list.png" width="800">

ESP-NOW 根据数据流定义了两个角色，发起者和响应者。同一设备可以同时担任两个角色。在物联网系统中，通常开关、传感器、液晶屏等为发起者，而灯、插座等智能应用为响应者。

<img src="docs/_static/en/device_role.png" width="750">

## 待办事项
  - [ ] 网关
  - [ ] 低功耗
  - [ ] 跳频

### IDF 版本

下表显示了 ESP-NOW 当前支持的 ESP-IDF 版本。标签 ![alt text](docs/_static/yes-checkm.png "支持") 表示支持，标签 ![alt text](docs/_static/no-icon.png) 表示不支持。

ESP-IDF 的 master 分支被标记为不支持，因为该分支引入的主要变动可能与 ESP-NOW 产生冲突。然而，不受这些变动影响的 ESP-NOW 示例仍可在 IDF 的 master 分支上正常运行。

终止维护的 IDF 分支被标记为不支持，例如 ESP-IDF Release/v4.0。如需获取详细信息，请参阅 [IDF 支持期限](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/versions.html#support-periods)。


|                       | ESP-IDF <br> Release/v4.1| ESP-IDF <br> Release/v4.2| ESP-IDF <br> Release/v4.3| ESP-IDF <br> Release/v4.4 | ESP-IDF <br> Release/v5.0 | ESP-IDF <br> Release/v5.1 | ESP-IDF <br> Release/v5.2 | ESP-IDF <br> Master |
|:----------- | :---------------------:|:---------------------: | :---------------------:| :---------------------:| :---------------------:| :---------------------:| :---------------------:| :---------------------:|
| ESP-NOW <br> Master  | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持") |![alt text](docs/_static/yes-checkm.png "支持") |
| ESP-NOW <br> v2.x.x  | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/no-icon.png "不支持")  | ![alt text](docs/_static/yes-checkm.png "支持") <sup> **2** </sup> | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持")  | ![alt text](docs/_static/yes-checkm.png "支持") |![alt text](docs/_static/yes-checkm.png "支持") |
| ESP-NOW <br> v1.0  | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持") | ![alt text](docs/_static/yes-checkm.png "支持") <sup> **1** </sup> | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/no-icon.png "不支持") | ![alt text](docs/_static/no-icon.png "不支持") |![alt text](docs/_static/no-icon.png "不支持") |


**备注 1:** ESP-NOW v1.0 内置的 IDF 分支目前是 IDF Release/v4.4。

**备注 2:** ESP-NOW v2.x.x 内置的 IDF 分支目前来自 IDF Release/v4.4。

### 支持期限

新特性开发和错误修复通常在主分支上进行，并将发布到 [ESP 组件注册表](https://components.espressif.com/)。每个版本的发布时间通常间隔几个月。当 ESP-NOW 的新版本发布时，请计划升级到该版本。

## 快速入门
### 硬件准备
推荐使用 ESP32、ESP32-C3、ESP32-S2、ESP32-S3、ESP32C2、ESP32-C6 芯片。

### 设置开发环境
设置环境并获取 ESP-IDF（release/v4.4 或 tag v4.4），请按照 [以下步骤](https://docs.espressif.com/projects/esp-idf/zh_CN/release-v4.4/esp32/index.html) 操作。

### 获取 ESP-NOW
按如下步骤下载 ESP-NOW：
```shell
git clone https://github.com/espressif/esp-now.git
```

### 编译和烧录固件
如果第一次使用此功能，建议先清除 flash，然后再烧录固件。步骤如下：
```shell
$ cd /path/to/esp-now/examples/get-started/
$ export ESPPORT=/dev/tty.SLAB_USBtoUART
$ idf.py set-target esp32c3
$ idf.py erase_flash
$ idf.py flash monitor
```

## 功能概述
### 控制
ESP-NOW 的优势：

1. **快速响应**：开机后，设备无需任何无线连接即可传输数据并控制其他配对设备，响应速度以毫秒为单位。
2. **良好的兼容性**：当设备连接到路由器或在热点模式下工作，也可以通过 ESP-NOW 实现快速稳定的通信。即使路由器出现故障或网络不稳定，设备也可以通过 ESP-NOW 保持稳定连接。
3. **远距离通信**：ESP-NOW 支持远距离通信。它适用于户外场景，即使有墙壁、地板阻隔，也能使设备保持稳定的连接。
4. **多跳控制**：ESP-NOW 可以实现设备的多跳控制。通过单播、广播和群控等方式，可以控制数百台设备。

### 配网

除了 Wi-Fi 配网和蓝牙配网外，ESP-NOW 还提供了一种新的配网方法。首先，通过蓝牙为第一台设备配网，其他设备不需要配置 SSID/密码信息，因为连接到网络的第一台设备可以直接将这些信息发送给其他设备。用户可以在 APP 端选择是否允许其他设备访问网络。

### 升级
ESP-NOW 可用于海量数据传输，如固件升级。

1. **从断点处恢复升级**：使用 ESP-NOW 升级固件时，固件会以固定大小分包，逐个写入 flash，设备会记录升级后的包。如果升级过程发生中断，设备将仅请求剩余的固件包，并从断点处继续升级操作。
2. **多设备升级**：ESP-NOW 可支持多台设备同时升级。3 分钟内可升级 50 台设备。
3. **回滚**：如果发生升级错误，固件可以回滚到之前的版本。

### 调试

ESP-NOW 可以用来接收设备运行时的日志，便于设备调试。它适用于因高压电、高温而无法直接接触设备的场景。通过多对多连接，发起者可以接收来自多个响应者的日志，快速诊断设备故障。

1. 设备日志
    - 日志分析：从日志中分析运行时间和重启次数。
    - 日志存储：将获取的数据存储在 SD 卡中或导出到网络。
    - 日志级别修改：每个函数模型的日志级别可以自由调整。
2. 调试命令
    - 外设调试：可以发送控制命令来测试 GPIO、UART、LED 等外设。
    - Wi-Fi 调试：国家地区代码、Wi-Fi 模式、Wi-Fi 功率等都可以设置，还可以调试 Wi-Fi 的性能。
    - 状态调试：重启命令、复位命令、存储和任务运行状态命令。
    - 自定义命令
3. 产品测试
    - 老化测试：设备随机重启，长期监测。
    - 干扰测试：向网络发送大量的 Wi-Fi 数据包，进行网络干扰测试。
    - 无线测试：监控射频性能、ping 包测试、距离和射频性能测试。
    - 模块测试：flash 读取时间测试、时间精度测试。
    - 版本验证：验证设备的出厂版本是否为指定版本。

### 数据加密

ESP-NOW 可以通过 ECDH 和 AES128-CCM 来保护数据安全。

1. **快速配置**：5 秒钟内可配置 16 台设备。
2. **多设备握手**：ESP-NOW 作为发起者的设备可以同时与其他多个设备握手。
3. **安全性**：
    - 使用 ECDH 和所有权证明 (PoP) 字符串授权会话、生成共享密钥
    - 使用 AES256-CTR 模式加密配置数据
    - 使用 AES128-CCM 模式加密 ESP-NOW 数据

## 相关文档和资源
- [ESP-NOW API 指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/api-reference/network/esp_now.html)
- [在 Arduino 环境下使用 ESP-NOW](https://github.com/yoursunny/WifiEspNow)
- [esp32.com 论坛](https://esp32.com/) 用于交流问题和获取社区资源。
- 如需反馈问题或有功能请求，请前往[Github 上的 Issues 板块](https://github.com/espressif/esp-now/issues)。在提交新 Issue 前，请先查看已有的 Issue。
- [ESP-FAQ](https://docs.espressif.com/projects/espressif-esp-faq/zh_CN/latest/) ，包括一些常见问题的解答。如果 ESP-NOW 无法正常启用，请前往 ESP-FAQ 进行故障排除。
