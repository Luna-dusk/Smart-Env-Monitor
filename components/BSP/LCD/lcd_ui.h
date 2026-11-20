#ifndef __LCD_UI_H__
#define __LCD_UI_H__

/**
 * @file  lcd_ui.h
 * @brief LCD 对外接口：初始化、刷新任务、跨任务状态写入
 *
 * 分层：
 *   业务层  app_* / sensor_task / network_task → 只调 lcd_ui_set_* 与 screen_home_update()
 *   接口层  本文件
 *   实现层  lcd_ui.c（SPI/ST7789/LVGL 移植层 + 渲染引擎）
 *           ui_theme.c（主题/背景/假玻璃）+ screen_home.c（仪表盘布局）
 *
 * 2026-11-18 重构：原来的"守护环 + 语音对话屏"整套已移除。
 * 本工程是环境监测仪，界面是单页仪表盘（ui/ui.md），不再有对话屏、
 * 也没有 lcd_ui_chat_* 系列接口。表情枚举仅为兼容引擎保留。
 */

#include <stdbool.h>

/** 屏幕表情（已废弃：仪表盘没有表情区，仅 screen_home_set_emotion 空实现兜底） */
typedef enum {
    LCD_EMO_SMILE  = 0,
    LCD_EMO_CRY    = 1,
    LCD_EMO_NORMAL = 2,
    LCD_EMO_ANGRY  = 3,
    LCD_EMO_DROOL  = 4,
} lcd_emotion_t;

/** 链路状态：仪表盘上只体现为状态栏 WiFi 图标的亮/灰 */
typedef enum {
    LCD_STATUS_STANDBY    = 0,   /* 待机（已联网）*/
    LCD_STATUS_CONNECTING = 1,   /* 联网中 → WiFi 图标灰显 */
    LCD_STATUS_LISTENING  = 2,   /* 预留（无语音功能）*/
    LCD_STATUS_REPLYING   = 3,   /* 预留（无语音功能）*/
} lcd_status_t;

/**
 * @brief 初始化 LCD：SPI 总线 → esp_lcd ST7789 → LVGL → 主题 → 仪表盘主屏
 * @note  须在 app_main 里、建 lcd_ui_task 之前调用；
 *        失败只打日志并返回（后续 set 调用只写影子变量，不会崩）
 */
void lcd_ui_init(void);

/**
 * @brief UI 渲染任务：泵 lv_timer_handler 并按需让出 CPU
 * @param arg 未使用
 * @note  栈 8192、优先级 8。传感器/天气数据由别的任务调 screen_home_update()
 *        投递，本任务只负责把影子变量搬上屏，不阻塞等待任何外设。
 */
void lcd_ui_task(void *arg);

/* 线程安全：任意任务可调用，下一帧生效 */
/** @brief 设置链路状态（只影响状态栏 WiFi 图标颜色） */
void lcd_ui_set_status(lcd_status_t st);

/** @brief 设置屏幕表情（空实现，仪表盘无表情区） */
void lcd_ui_set_emotion(lcd_emotion_t emo);

#endif /* __LCD_UI_H__ */
