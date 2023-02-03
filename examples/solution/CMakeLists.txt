# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctlycmake_minimum_required(VERSION 3.5)
cmake_minimum_required(VERSION 3.5)
add_compile_options(-fdiagnostics-color=always)

if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32")
endif()

# include(${ESPNOW_PATH}/component.cmake)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(DEFINED ENV{PROJECT_NAME})
  set(PROJECT_NAME $ENV{PROJECT_NAME})
else()
  set(PROJECT_NAME "Init")
endif(DEFINED ENV{PROJECT_NAME})

project(${PROJECT_NAME})

git_describe(PROJECT_VERSION ${COMPONENT_DIR})
message("Project commit: " ${PROJECT_VERSION})
