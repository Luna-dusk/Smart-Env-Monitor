# 组件依赖统一走 REQUIRES。
#
# 原来这里写的是 target_link_libraries(${COMPONENT_LIB} INTERFACE nvs_flash ...)，
# 那是把裸库名直接塞给链接器，最终变成 -lnvs_flash / -lesp_wifi …，
# 而 IDF 的真实产物叫 libnvs_flash.a 之类、且在不同目录，链接器找不到，
# 报 "cannot find -lnvs_flash"。INTERFACE 只适合传编译选项/头文件路径，
# 传组件名必须用 REQUIRES（或 PRIV_REQUIRES）。
idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES BSP nvs_flash esp_wifi esp_event esp_netif esp_http_client json mbedtls)
