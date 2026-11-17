#ifndef __IIC_H__
#define __IIC_H__

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"   /* IDF5 没有 FreeRTOS/task.h 这个路径（原写法编译不过） */
#include "u8g2.h"
#define I2C_SDA_PIN GPIO_NUM_11 
#define I2C_SCL_PIN GPIO_NUM_10
extern i2c_master_dev_handle_t i2c_dev_handle;
void i2c_init(void);

#endif