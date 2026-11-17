set(src_dirs
                "LED_Driver"
                "I2C"
                "OLED"
                "UART"
                "WIFI"
                "LCD")

set(include_dirs
                "LED_Driver"
                "I2C"
                "OLED"
                "UART"
                "WIFI"
                "LCD")

set(requires
                driver
                esp_driver_gpio
                esp_driver_i2c
                esp_driver_spi
                nixy4__u8g2          # OLED 驱动（I2C/iic.h、OLED/oled.c 直接 include u8g2.h）
                nvs_flash
                esp_netif
                esp_wifi
                esp_event
                esp_http_server      # 配网门户 httpd（WIFI/wifi_provision.c）
                lwip                 # wifi.c 联网诊断的 socket/select
                esp_timer
                esp_lcd              # ST7789 SPI 面板驱动（LCD/lcd_ui.c）
                lvgl__lvgl           # LVGL 8.3（main/idf_component.yml 里 ~8.3.0）
                )

# 配网页 portal.html 内嵌进固件（wifi_provision.c 引用 _binary_portal_html_*）
idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires}
                       EMBED_TXTFILES "WIFI/portal.html")
component_compile_options(-ffast-math -O3 -Wno-error=format -Wno-format)