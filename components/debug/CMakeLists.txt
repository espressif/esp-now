idf_component_register(SRC_DIRS "src" "src/sdcard" "src/commands" "src/commands/pcap"
                       INCLUDE_DIRS "include" "include/sdcard"
                       PRIV_INCLUDE_DIRS "src" "src/commands/pcap"
                       REQUIRES  "utils" "spi_flash" "console" "nvs_flash" "fatfs" "espnow" "spiffs" "debug" "provisioning" "control" "ota")

set(WRAP_FUNCTIONS esp_log_write esp_log_writev)
foreach(func ${WRAP_FUNCTIONS})
     target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${func}")
endforeach()