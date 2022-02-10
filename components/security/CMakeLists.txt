idf_component_register(SRC_DIRS "src" "src/protocomm/security"
                       INCLUDE_DIRS "include" "include/protocomm/security" 
                       REQUIRES  "mbedtls" "espnow" "protobuf-c" "protocomm")

idf_component_get_property(COMPONENT_DIR protocomm COMPONENT_DIR)
target_include_directories(${COMPONENT_TARGET} PUBLIC
"${COMPONENT_DIR}/proto-c"
)
