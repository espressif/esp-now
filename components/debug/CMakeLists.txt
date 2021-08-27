idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES  "utils" "spi_flash" "console" "nvs_flash" "fatfs" "espnow" "spiffs")

set(WRAP_FUNCTIONS esp_log_write esp_log_writev)
foreach(func ${WRAP_FUNCTIONS})
     target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${func}")
endforeach()