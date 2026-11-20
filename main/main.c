#include <stdio.h>
#include "LED.h"
#include "lcd_ui.h"          /* LCD/：ST7789 + LVGL 移植层（业务只认这个头文件） */
#include "screen_home.h"     /* LCD/：仪表盘数据接口 screen_home_update() */
#include "wifi.h"            /* WIFI/：STA 连接 + 指数退避重连 */
#include "wifi_provision.h"  /* WIFI/：SoftAP 网页配网请求位 */
#include "sntp_time.h"       /* WIFI/：SNTP 对时（WiFi 连上后启动） */
#include "weather.h"         /* WIFI/：和风天气客户端（30min 轮询） */
#include "buzzer.h"          /* ALARM/：无源蜂鸣器 LEDC PWM */
#include "alarm.h"           /* ALARM/：闹钟调度（到点起铃 / 按键停铃） */
#include "tvoc301.h"         /* App_sensors/：TVOC-301 空气质量（UART2） */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

/* BOOT 键（GPIO0，按下为低）长按 3s → 重新进入网页配网。
 * 原工程由 BSP/KEY 的按键扫描任务触发；本工程没有按键模块，
 * 这里用 20ms 轮询单独实现，只碰 wifi_provision_request() 一个接口
 * （真正的模式切换仍然发生在 wifi 任务里，避免多任务改 WiFi 状态）。
 *
 * 短按（<800ms）留给闹钟：响铃中 = 停铃，否则 = 开关闹钟。
 * 这样单键就能覆盖"早上被吵醒按一下"和"平时临时关掉"两种操作。 */
#define BOOT_BTN_GPIO        GPIO_NUM_0
#define BOOT_LONG_PRESS_MS   3000
#define BOOT_SHORT_PRESS_MS  800
#define BOOT_DEBOUNCE_MS     50

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

            /* 有 IP 了才能对时。这一步之前 time() 是 1970，
             * 屏幕上的时钟会一直显示 --:--（不是显示 bug，是真的没时间） */
            sntp_time_start();

            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "等 WiFi 超时，屏幕保持 CONNECTING（配网页仍可用）");
    vTaskDelete(NULL);
}

/**
 * @brief  BOOT 键检测（20ms 轮询）
 *
 * 三种结局，互斥：
 *   · 按住 ≥3s      → 请求网页配网（只置标志位，真正的切换在 wifi 任务里）
 *   · 短按 <800ms   → 响铃中：停铃；否则：切换闹钟开关
 *   · 800ms~3s      → 什么都不做（留白区间，避免"差一点就到 3s"时误触）
 *
 * @note   只有**松手那一刻**才判短按，这样长按过程中不会先触发一次开关。
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
        bool    pressed = gpio_get_level(BOOT_BTN_GPIO) == 0;
        int64_t now     = esp_timer_get_time();

        if (pressed) {
            if (press_start == 0) {
                press_start = now;
            } else if (!latched &&
                       now - press_start >= (int64_t)BOOT_LONG_PRESS_MS * 1000) {
                latched = true;
                ESP_LOGI(TAG, "BOOT 长按 -> 请求网页配网");
                wifi_provision_request();
            }
        } else if (press_start != 0) {
            int64_t held_ms = (now - press_start) / 1000;
            if (!latched &&
                held_ms >= BOOT_DEBOUNCE_MS && held_ms < BOOT_SHORT_PRESS_MS) {
                if (alarm_is_ringing()) {
                    alarm_dismiss();
                    ESP_LOGI(TAG, "BOOT 短按 -> 停止闹铃");
                } else {
                    bool on = !alarm_is_enabled();
                    alarm_set_enabled(on);
                    ESP_LOGI(TAG, "BOOT 短按 -> 闹钟%s", on ? "开启" : "关闭");
                }
            }
            press_start = 0;
            latched     = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/**
 * @brief  仪表盘数据泵：1s 采集一次，打包成快照推给 UI
 * @param  arg 未使用
 *
 * 这是 sensor_task 的雏形。当前接了 TVOC-301、闹钟快照和链路状态，
 * DHT11 / 天气还没实现，对应字段填 *_valid = false，
 * 界面会显示 "--" 而不是假数据。
 *
 * @note screen_home_update() 内部只写影子变量 + 置脏，不会阻塞本任务；
 *       真正改控件发生在 lcd_ui 任务的 500ms lv_timer 里。
 */
static void ui_feed_task(void *arg)
{
    (void)arg;

    for (;;) {
        ui_dashboard_data_t d = {0};

        /* 空气质量：TVOC-301 走 UART2 主动上报，1s 一帧。
         * 超过 5s 没新帧就当作无数据（模块被拔掉 / 供电掉了）。 */
        tvoc301_data_t tv;
        if (tvoc301_latest(&tv) && tvoc301_age_ms() < 5000) {
            d.tvoc_ugm3 = tv.tvoc_ugm3;
            d.hcho_ugm3 = tv.hcho_ugm3;
            d.eco2_ppm  = tv.eco2_ppm;
            d.air_valid = true;
        }

        /* 温湿度：尚未实现 */
        d.dht_valid = false;

        /* 天气：模块内部自己等 WiFi，这里只读最后一次成功的结果。
         * 拉取失败时会保留旧值，所以天气卡不会莫名其妙变回 "--"。 */
        weather_data_t wx;
        if (weather_get(&wx) && wx.valid) {
            snprintf(d.cond_text, sizeof(d.cond_text), "%s", wx.cond_text);
            d.temp_now      = wx.temp_now;
            d.temp_min      = wx.temp_min;
            d.temp_max      = wx.temp_max;
            snprintf(d.city, sizeof(d.city), "%s", wx.city);
            d.weather_valid = true;
        } else {
            d.weather_valid = false;
        }

        /* 闹钟：时间和开关都由 ALARM 模块持有，这里只读快照。
         * 关闭状态下时间仍会推给 UI —— 底栏要显示"设的是几点"，
         * 否则永远是 --:--（这就是之前看着像占位的原因）。 */
        alarm_get(&d.alarm_hour, &d.alarm_min, &d.alarm_on);

        /* 链路状态 */
        EventGroupHandle_t ev = wifi_get_event_group();
        d.wifi_ok = (ev != NULL) && ((xEventGroupGetBits(ev) & WIFI_CONNECTED_BIT) != 0);
        d.sntp_ok = sntp_time_is_synced();

        screen_home_update(&d);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    LED_Init();             /* 初始化LED（配网时会由 wifi_provision 接管闪烁/常亮） */
    buzzer_init();          /* 蜂鸣器 PWM（GPIO18），上电静音 */
    alarm_init();           /* 闹钟调度任务（默认 07:00，1s 比对一次） */
    tvoc301_init();         /* 空气质量传感器 UART2（GPIO16/17），等模组主动上报 */
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
    /* 天气：任务内部自己等 WiFi 连上，这里创建顺序不敏感。
     * 未填 API Host/Key 时函数直接返回，界面天气卡保持 "--"。 */
    weather_init();
    if (xTaskCreate(boot_key_task, "boot_key", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create boot_key task failed");
    }

    /* 仪表盘数据泵：1s 周期把传感器/链路状态推给 UI */
    if (xTaskCreate(ui_feed_task, "ui_feed", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create ui_feed task failed");
    }

    ESP_LOGI(TAG, "app_main tasks running");

    vTaskDelete(NULL);
}