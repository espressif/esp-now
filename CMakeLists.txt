set(srcs "")
set(requires "")
set(include_dirs "")

list(APPEND srcs         "src/control/src/espnow_ctrl.c")
list(APPEND include_dirs "src/control/include")

list(APPEND srcs         "src/debug/src/espnow_console.c"
                         "src/debug/src/espnow_log_flash.c"
                         "src/debug/src/espnow_log.c")
list(APPEND srcs         "src/debug/src/sdcard/sdcard.c")
list(APPEND srcs         "src/debug/src/commands/pcap/pcap.c"
                         "src/debug/src/commands/cmd_espnow.c"
                         "src/debug/src/commands/cmd_iperf.c"
                         "src/debug/src/commands/cmd_peripherals.c"
                         "src/debug/src/commands/cmd_sdcard.c"
                         "src/debug/src/commands/cmd_system.c"
                         "src/debug/src/commands/cmd_wifi_sniffer.c"
                         "src/debug/src/commands/cmd_wifi.c"
                         "src/debug/src/commands/espnow_commands.c")
list(APPEND include_dirs "src/debug/include"
                         "src/debug/include/sdcard"
                         "src/debug/src"
                         "src/debug/src/commands/pcap")
list(APPEND requires "console" "fatfs" "spiffs")

list(APPEND srcs         "src/espnow/src/espnow.c"
                         "src/espnow/src/espnow_group.c")
list(APPEND include_dirs "src/espnow/include")

list(APPEND srcs         "src/ota/espnow_ota_initiator.c")
list(APPEND srcs         "src/ota/espnow_ota_responder.c")
list(APPEND include_dirs "src/ota/include")
list(APPEND requires "app_update" "esp_http_client" "esp_https_ota")

list(APPEND srcs         "src/provisioning/src/espnow_prov.c")
list(APPEND include_dirs "src/provisioning/include")

list(APPEND srcs         "src/security/src/espnow_security.c"
                         "src/security/src/espnow_security_initiator.c"
                         "src/security/src/espnow_security_responder.c"
                         "src/security/src/protocomm/security/client_security1.c")
list(APPEND include_dirs "src/security/include" "src/security/include/protocomm/security")
list(APPEND requires "mbedtls" "protobuf-c" "protocomm")

list(APPEND srcs         "src/utils/src/esp_mem.c"
                         "src/utils/src/esp_reboot.c"
                         "src/utils/src/esp_storage.c"
                         "src/utils/src/esp_timesync.c"
                         "src/utils/src/esp_utils.c")
list(APPEND include_dirs "src/utils/include")

list(APPEND requires "spi_flash" "nvs_flash" "esp_timer" "esp_wifi" "driver")

idf_component_register(SRCS "${srcs}"
                       INCLUDE_DIRS "${include_dirs}"
                       REQUIRES "${requires}")

idf_component_get_property(COMPONENT_DIR protocomm COMPONENT_DIR)
target_include_directories(${COMPONENT_TARGET} PUBLIC
"${COMPONENT_DIR}/proto-c"
)

set(WRAP_FUNCTIONS esp_log_write esp_log_writev)
foreach(func ${WRAP_FUNCTIONS})
     target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${func}")
endforeach()

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-pointer-sign -Wno-format)