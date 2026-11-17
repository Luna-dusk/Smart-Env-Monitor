#include "uart.h"
#include "oled.h"
#include "driver/gpio.h"

void uart_init()
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
        .rx_flow_ctrl_thresh = 122
    };
    uart_param_config(UART_UX, &uart_config);
    uart_set_pin(UART_UX, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_UX, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2, 20, NULL, 0);
}

void uart_receive_task(void *pvParameters)
{
    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE);
    if (data == NULL) {
        vTaskDelete(NULL);
        return;
    }
    //光标当前位置
    uint8_t cursor_x = 0;
    uint8_t cursor_y = 0;
    uint8_t flag = 1;
    int msg_w, max_w, i;

    while(1)
    {
        int len = uart_read_bytes(UART_NUM_0, data, RX_BUF_SIZE - 1, 1000);
        if (len > 0)
        {
            data[len] = 0;

            //去掉末尾的 \r \n，否则它们会被当成空格显示
            i = len - 1;
            while (i >= 0 && (data[i] == '\r' || data[i] == '\n')) {
                data[i] = 0;
                i--;
            }
            if (i < 0) continue;  // 整条消息都是换行符，跳过

            msg_w = strlen((const char *)data) * 6;//写入数据的长度

            if (flag) {
                oled_clear();
                cursor_x = 0;
                cursor_y = 0;
                flag = 0;
            }
            else if (cursor_x > 0) 
            {
                if (cursor_x + 6 + msg_w > 128) 
                {
                    cursor_x = 0;
                    cursor_y += 8;
                }
                else 
                {
                    oled_show_char(cursor_x, cursor_y, ' ');
                    cursor_x += 6;
                }
            }

            if (cursor_y >= 64) 
            {
                oled_clear();
                oled_show_string(0, 0, "Message is too long");
                oled_show_string(0, 8, "OLED Clear");
                oled_show();
                vTaskDelay(1000);
                oled_clear();
                cursor_x = 0;
                cursor_y = 0;
            }
            oled_show_string(cursor_x, cursor_y, (const char *)data);
            max_w = 128 - cursor_x;//从当前光标位置到屏幕右边缘剩余可用的水平像素数
            cursor_x += (msg_w < max_w) ? msg_w : max_w;
            oled_show();
        }
        vTaskDelay(10);
    }
    free(data);
}
