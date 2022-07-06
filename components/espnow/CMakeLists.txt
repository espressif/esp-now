idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES "utils" "security")
target_compile_options(${COMPONENT_LIB} PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_NONE)
