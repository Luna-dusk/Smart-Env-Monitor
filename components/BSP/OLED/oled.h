#ifndef __OLED_H__
#define __OLED_H__

#include "iic.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "u8g2.h"              // U8g2 核心头文件
#include "esp32_hw_i2c.h"   // ESP32 I2C 硬件抽象层（nixy4/u8g2 包里的端口头，原名 u8g2_port_* 不存在）

void oled_init(void);                              // 初始化 OLED
void oled_clear(void);                             // 清屏
void oled_show(void);                              // 刷新显示（把缓冲区刷到屏幕）
void oled_draw_pixel(uint8_t x, uint8_t y);        // 画一个点
void oled_show_char(uint8_t x, uint8_t y, char c); // 在 (x,y) 显示一个字符
void oled_show_string(uint8_t x, uint8_t y, const char *str); // 显示字符串

#endif
/*
中文显示方法：
// 1. 初始化 u8g2 结构体
    // 注意: 使用 u8g2_Setup_ssd1306_i2c_128x64_noname_f
    // 并直接传入 u8x8_byte_esp32_hw_i2c 和 u8x8_gpio_and_delay_esp32_i2c 作为回调
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,                    // 旋转角度
        u8x8_byte_esp32_hw_i2c,     // 硬件 I2C 字节发送回调
        u8x8_gpio_and_delay_esp32_i2c // GPIO 和延时回调
    );

    // 2. 设置 I2C 地址 (非常重要!)
    // 传入的是 8-bit 地址，即 7-bit 地址左移一位
    u8x8_SetI2CAddress(&u8g2.u8x8, I2C_OLED_ADDRESS << 1);

    // 3. 初始化并点亮屏幕
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    // 4. 启用 UTF-8 支持 (用于显示中文)
    u8g2_EnableUTF8Print(&u8g2);

    // 5. 设置字体并显示
    u8g2_SetFont(&u8g2, u8g2_font_wqy16_t_gb2312); // 中文
    // u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr); // 英文

    u8g2_ClearBuffer(&u8g2);
    u8g2_SetCursor(&u8g2, 0, 20);
    u8g2_Print(&u8g2, "Hello, ESP-IDF!");
    
    u8g2_SetCursor(&u8g2, 0, 50);
    u8g2_Print(&u8g2, "你好，世界！");
    
    u8g2_SendBuffer(&u8g2);
*/