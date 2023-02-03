if(CONFIG_APP_WIFI_PROVISION)
    list(APPEND srcs "wifi_prov.c")
endif()

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES "wifi_provisioning" "qrcode")
