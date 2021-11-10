idf_component_register(SRC_DIRS "src" "src/protocomm/security"
                       INCLUDE_DIRS "include" "include/protocomm/security" 
                       REQUIRES  "mbedtls" "espnow" "protobuf-c" "protocomm")
