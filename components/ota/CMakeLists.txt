idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "include"
                       REQUIRES  "spi_flash" "app_update" "nvs_flash" "utils" "esp_http_client" "esp_https_ota" "espnow")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-pointer-sign -Wno-format)