
# if (CONFIG_AT_WEB_SERVER_SUPPORT)
#     if(NOT CONFIG_AT_WEB_USE_FATFS)
#         set(embed_txt_files ../fs_image/index.html)
#     endif()
# endif()

idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES ${embed_txt_files})
