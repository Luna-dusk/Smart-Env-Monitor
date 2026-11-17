#ifndef _UART_H
#define _UART_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/uart_select.h"
#include "driver/gpio.h"

#define UART_UX UART_NUM_0
#define UART_TX_PIN GPIO_NUM_43
#define UART_RX_PIN GPIO_NUM_44

#define RX_BUF_SIZE 1024

void uart_init();
void uart_receive_task(void *pvParameters);

#endif // _UART_H
