set(src_dirs
                "LED_Driver"
                "WIFI"
                "LCD"
                "ALARM"          # buzzer.c 无源蜂鸣器（LEDC PWM）
                "App_sensors")   # tvoc301.c 空气质量传感器（UART2）

set(include_dirs
                "LED_Driver"
                "WIFI"
                "LCD"
                "ALARM"
                "App_sensors")

set(requires
                driver
                esp_driver_gpio
                esp_driver_spi
                esp_driver_uart     # TVOC-301 串口（App_sensors/tvoc301.c）
                esp_driver_ledc     # 蜂鸣器 PWM（ALARM/buzzer.c）
                nvs_flash
                esp_netif
                esp_wifi
                esp_event
                log
                esp_http_server      # 配网门户 httpd（WIFI/wifi_provision.c）
                esp_http_client      # 天气客户端（WIFI/weather.c）
                json                 # cJSON：解析和风天气响应
                mbedtls              # esp_crt_bundle_attach：HTTPS 根证书包
                lwip                 # wifi.c 联网诊断的 socket/select
                esp_timer
                esp_lcd              # ST7789 SPI 面板驱动（LCD/lcd_ui.c）
                lvgl__lvgl           # LVGL 8.3（main/idf_component.yml 里 ~8.3.0）
                )

# 配网页 portal.html 内嵌进固件（wifi_provision.c 引用 _binary_portal_html_*）
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires}
                       EMBED_TXTFILES "WIFI/portal.html")
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)