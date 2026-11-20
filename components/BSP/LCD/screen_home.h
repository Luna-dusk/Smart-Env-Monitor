#ifndef __SCREEN_HOME_H
#define __SCREEN_HOME_H

/**
 * @file  screen_home.h
 * @brief 主屏 · 环境监测仪表盘（240×320 单页，不滚动不翻页）
 *
 * 布局严格对照 ui/ui.md 与 ui/预览图片/ 下的三张预览图：
 *
 *   y   0~ 23  状态栏   WiFi / 闹钟铃铛 / 电池
 *   y  26~100  时钟区   Montserrat48 "12:34" + 16px "2026-11-18 星期三"
 *   y 104~152  温湿度卡 左 25°C / 右 45%，中间 1px 分隔线
 *   y 156~214  空气卡   TVOC / HCHO / eCO2 + 10 格等级色条 + "轻度污染"
 *   y 218~266  天气卡   多云 25°C      右对齐 18~27°C / 广州
 *   y 270~318  底栏     铃铛 07:00 ON | 对勾 SNTP 已同步
 *
 * 线程模型（与 lcd_ui.c 的影子变量同一套路）：
 *   - screen_home_set_* / update()  **任意任务可调**，内部只写影子变量 + 置脏；
 *   - 真正的 lv_* 调用全部发生在 lcd_ui 任务里 —— 由本模块注册的 lv_timer
 *     在 lv_timer_handler() 中被驱动，天然就在那个任务上下文中。
 *   - 唯一例外是 screen_home_set_clock()：它只被 lcd_ui 任务的 clock_tick()
 *     每秒调用一次，直接改 label，省一层中转。
 */

#include "lvgl.h"
#include "lcd_ui.h"       /* lcd_status_t / lcd_emotion_t */
#include <stdint.h>
#include <stdbool.h>

/** 空气质量等级（依据 TVOC 粗分档，同时参考 eCO2） */
typedef enum {
    UI_AIR_EXCELLENT = 0,   /* TVOC < 200  优 */
    UI_AIR_GOOD,            /* 200 ~ 500   良 */
    UI_AIR_LIGHT,           /* 500 ~ 1000  轻度污染 */
    UI_AIR_HEAVY,           /* >= 1000     重度污染 */
} ui_air_level_t;

/** 仪表盘全量数据快照：一次 update 刷一屏，不要拆成十几次调用 */
typedef struct {
    /* 温湿度（DHT11） */
    int8_t   temp_c;
    uint8_t  humi_rh;
    bool     dht_valid;

    /* 空气质量（TVOC-301） */
    uint16_t tvoc_ugm3;
    uint16_t hcho_ugm3;
    uint16_t eco2_ppm;
    bool     air_valid;

    /* 天气（HTTP 轮询） */
    char     cond_text[16];     /* "多云" */
    int      temp_now;
    int      temp_min;
    int      temp_max;
    char     city[24];
    bool     weather_valid;

    /* 闹钟 */
    uint8_t  alarm_hour;
    uint8_t  alarm_min;
    bool     alarm_on;

    /* 链路状态 */
    bool     wifi_ok;
    bool     sntp_ok;
} ui_dashboard_data_t;

/**
 * @brief 创建主屏并 load（内部会调用 lv_scr_load）
 * @note  只调用一次，必须在 ui_theme_init() 之后、lcd_ui 任务开始泵
 *        lv_timer_handler() 之前。会顺手注册一个 500ms 的刷新定时器。
 */
void screen_home_create(void);

/**
 * @brief 更新状态栏时钟文本（"12:34" 或 "--:--"）
 * @param hhmm 已格式化的字符串，内部会拷贝
 * @note  **仅限 lcd_ui 任务调用**（clock_tick 里），因为它直接改 label。
 */
void screen_home_set_clock(const char *hhmm);

/**
 * @brief 取当前屏上显示的时钟文本
 * @return 静态缓冲区指针，永不为 NULL；未设置过时返回 "--:--"
 */
const char *screen_home_get_clock(void);

/**
 * @brief 设置链路状态（影响状态栏 WiFi 图标亮/灰）
 * @note  LVGL 静态屏不画表情，lcd_emotion_t 语义已废弃，
 *        screen_home_set_emotion() 保留为空实现仅为兼容 lcd_ui 引擎。
 */
void screen_home_set_status(lcd_status_t st);

/** @brief 空实现（保留接口，仪表盘无表情区） */
void screen_home_set_emotion(lcd_emotion_t emo);

/**
 * @brief 推送一份数据快照（任意任务可调，非阻塞）
 * @param d 快照，内部整体拷贝；传 NULL 会被忽略
 * @note  只写影子变量并置脏，真正上屏发生在下一个刷新周期（≤500ms）。
 *        字段值为 false 的 *_valid 会让对应区域显示 "--" 并灰显。
 */
void screen_home_update(const ui_dashboard_data_t *d);

#endif /* __SCREEN_HOME_H */
