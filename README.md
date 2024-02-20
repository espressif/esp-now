# ESP-NOW Component

[![Component Registry](https://components.espressif.com/components/espressif/esp-now/badge.svg)](https://components.espressif.com/components/espressif/esp-now)

- [User Guide](https://github.com/espressif/esp-now/tree/master/User_Guide.md)

esp-now supports one-to-many and many-to-many device connection and control which can be used for the mass data transmission, like network config, firmware upgrade and debugging etc.

### Add component to your project

Please use the component manager command `add-dependency` to add the `esp-now` to your project's dependency, during the `CMake` step the component will be downloaded automatically.

```
idf.py add-dependency "espressif/esp-now=*"
```

## Example

Please use the component manager command `create-project-from-example` to create the project from example template.

```
idf.py create-project-from-example "espressif/esp-now=*:coin_cell_demo/bulb"
```

Then the example will be downloaded in current folder, you can check into it for build and flash.

> You can use this command to download other examples. Or you can download examples from esp-now repository: 
1. [coin_cell_demo/bulb](https://github.com/espressif/esp-now/tree/master/examples/coin_cell_demo/bulb)
2. [coin_cell_demo/switch](https://github.com/espressif/esp-now/tree/master/examples/coin_cell_demo/switch)
3. [control](https://github.com/espressif/esp-now/tree/master/examples/control)
4. [get-started](https://github.com/espressif/esp-now/tree/master/examples/get-started)
5. [ota](https://github.com/espressif/esp-now/tree/master/examples/ota)
6. [security](https://github.com/espressif/esp-now/tree/master/examples/security)
7. [solution](https://github.com/espressif/esp-now/tree/master/examples/solution)
8. [wireless_debug](https://github.com/espressif/esp-now/tree/master/examples/wireless_debug)

### Q&A

Q1. I encountered the following problems when using the package manager

```
Executing action: create-project-from-example
CMakeLists.txt not found in project directory /home/username
```

A1. This is because an older version packege manager was used, please run `pip install -U idf-component-manager` in ESP-IDF environment to update.
