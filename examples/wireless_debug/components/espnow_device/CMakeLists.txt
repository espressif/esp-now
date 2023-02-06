if(CONFIG_APP_ESPNOW_DEBUG_MONITOR)
    list(APPEND srcs "monitor.c")
endif()

if(CONFIG_APP_ESPNOW_DEBUG_MONITORED)
    list(APPEND srcs "monitored.c")
endif()

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "include"
                    REQUIRES "esp-now" "mdns" "web_server")