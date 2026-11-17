#include "iic.h"

i2c_master_dev_handle_t i2c_dev_handle;

void i2c_init(void)
{
    i2c_master_bus_config_t i2c_config={
        .i2c_port=-1,
        .sda_io_num=I2C_SDA_PIN,
        .scl_io_num=I2C_SCL_PIN,
        .clk_source=SOC_MOD_CLK_APB,
        .intr_priority=0,
    };
    i2c_master_bus_handle_t bus_handle;
    i2c_new_master_bus(&i2c_config,&bus_handle);

    i2c_device_config_t device_config={
        .dev_addr_length=I2C_ADDR_BIT_LEN_7,
        .device_address=0x3C,
        .scl_speed_hz=100000
    };
    i2c_master_bus_add_device(bus_handle,&device_config,&i2c_dev_handle);
}
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


