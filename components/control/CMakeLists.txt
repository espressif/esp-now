idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES "espnow")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-pointer-sign -Wno-format)