#include <stdio.h>
#include "LED.h"
#include "uart.h"
#include "iic.h"
#include "oled.h"
#include "lcd_ui.h"          /* LCD/：ST7789 + LVGL 移植层（业务只认这个头文件） */
#include "wifi.h"            /* WIFI/：STA 连接 + 指数退避重连 */
#include "wifi_provision.h"  /* WIFI/：SoftAP 网页配网请求位 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

void uart_receive_task(void *pvParameters);

static const char *TAG = "MAIN";

/* BOOT 键（GPIO0，按下为低）长按 3s → 重新进入网页配网。
 * 原工程由 BSP/KEY 的按键扫描任务触发；本工程没有按键模块，
 * 这里用 20ms 轮询单独实现，只碰 wifi_provision_request() 一个接口
 * （真正的模式切换仍然发生在 wifi 任务里，避免多任务改 WiFi 状态）。 */
#define BOOT_BTN_GPIO        GPIO_NUM_0
#define BOOT_LONG_PRESS_MS   3000

/**
 * @brief  等 WiFi 拿到 IP 后把屏幕状态栏从"联网中"切回"监护中"
 *
 * 事件组由 esp_create_wifi_task() 创建，所以必须轮询等句柄——直接取
 * 可能拿到 NULL（和原工程 main.c 的 time_sync_task 同一坑）。
 */
static void wifi_status_task(void *arg)
{
    (void)arg;

    for (int i = 0; i < 120; i++) {          /* 最多等 60s，之后不再改 UI */
        EventGroupHandle_t ev = wifi_get_event_group();
        if (ev != NULL && (xEventGroupGetBits(ev) & WIFI_CONNECTED_BIT)) {
            lcd_ui_set_status(LCD_STATUS_STANDBY);
            ESP_LOGI(TAG, "WiFi connected -> LCD status STANDBY");
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "等 WiFi 超时，屏幕保持 CONNECTING（配网页仍可用）");
    vTaskDelete(NULL);
}

/**
 * @brief  BOOT 长按检测（20ms 轮询，单次按住只触发一回）
 * @note   配网请求只置标志位；有凭证时也会被 wifi 任务的 1s 保活轮询取走。
 */
static void boot_key_task(void *arg)
{
    (void)arg;

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BOOT_BTN_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* BOOT 键对 GND，内部上拉 */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "boot key gpio cfg failed, 长按重配网不可用");
        vTaskDelete(NULL);
        return;
    }

    int64_t press_start = 0;   /* 0 = 未按下 */
    bool    latched     = false;

    for (;;) {
        bool pressed = gpio_get_level(BOOT_BTN_GPIO) == 0;
        if (pressed) {
            int64_t now = esp_timer_get_time();
            if (press_start == 0) {
                press_start = now;
            } else if (!latched &&
                       now - press_start >= (int64_t)BOOT_LONG_PRESS_MS * 1000) {
                latched = true;
                ESP_LOGI(TAG, "BOOT 长按 -> 请求网页配网");
                wifi_provision_request();
            }
        } else {
            press_start = 0;
            latched     = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    LED_Init();             /* 初始化LED（配网时会由 wifi_provision 接管闪烁/常亮） */
    i2c_init();             /* 初始化I2C (OLED通信) */
    oled_init();            /* 初始化OLED */
    uart_init();            /* 初始化UART */

    /* 在OLED上显示启动提示 */
    oled_show_string(0, 0, "UART OLED Ready");
    oled_show();

    /* ── LCD：先 init 再建任务。栈 8192 / 优先级 8
     *    （LVGL 非线程安全，所有 lv_* 只允许在 lcd_ui 任务里跑）── */
    lcd_ui_init();
    if (xTaskCreate(lcd_ui_task, "lcd_ui", 8192, NULL, 8, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create lcd_ui task failed");
    }

    /* ── WiFi：独立任务连接，不阻塞启动序列；无凭证会自动进配网热点 ── */
    lcd_ui_set_status(LCD_STATUS_CONNECTING);
    if (esp_create_wifi_task() != ESP_OK) {
        ESP_LOGE(TAG, "create wifi task failed");
    }
    if (xTaskCreate(wifi_status_task, "wifi_status", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create wifi_status task failed");
    }
    if (xTaskCreate(boot_key_task, "boot_key", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create boot_key task failed");
    }

    /* 创建UART接收任务，接收串口数据并显示到OLED */
    xTaskCreate(uart_receive_task, "uart_rx_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "app_main done: LCD + WiFi + UART tasks running");
    /* 原来 app_main 里的 LED 心跳循环已删：LED 现在归配网模块用
     * （配网中快闪、联网成功常亮），两个任务抢同一个 GPIO 会互相打架。 */
    vTaskDelete(NULL);
}