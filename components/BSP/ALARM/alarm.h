#ifndef __ALARM_H
#define __ALARM_H

/**
 * @file  alarm.h
 * @brief 闹钟调度：到点响铃、按键停铃、状态查询
 *
 * 本模块只管"什么时候响"，发声交给 buzzer.c（渐强任务已在那边实现）。
 * 一个后台任务 1s 比对一次本地时间，命中就调 buzzer_alarm_start()。
 *
 * 时间从哪来：SNTP 对时之后 time() 才有意义。没对时的时候本模块
 * 什么都不做（否则 1970 年的时间会让闹钟永远不响或者乱响）。
 *
 * 设置方式：设备只有 BOOT 一个按键，且长按 3s 已用于配网，
 * 所以闹钟时间先定在下面的宏里，改完重新烧录即可；
 * 运行期可以用 BOOT 短按开关闹钟。
 */

#include <stdint.h>
#include <stdbool.h>

/* 默认闹钟：每天 07:00，上电默认开启。想改点就改这两个数 */
#define ALARM_DEFAULT_HOUR      7
#define ALARM_DEFAULT_MIN       0

/* 响铃上限：没人按停就 60s 后自动静音，避免空房间一直吵 */
#define ALARM_RING_MAX_MS       60000

/**
 * @brief  启动闹钟调度任务（只调用一次）
 * @note   内部会注册 1s 周期的 FreeRTOS 任务，不占用 LVGL 任务。
 */
void alarm_init(void);

/**
 * @brief  修改闹钟时间（时分都在合法范围内才生效）
 * @param  hour 0~23
 * @param  min  0~59
 * @note   修改后会清掉"本分钟已触发"的记录，新设定的时间仍能正常响。
 */
void alarm_set(uint8_t hour, uint8_t min);

/** @brief 开/关闹钟（关闭不影响已显示的时间，只是不再触发） */
void alarm_set_enabled(bool on);

/** @brief 闹钟是否处于开启状态 */
bool alarm_is_enabled(void);

/** @brief 是否正在响铃（含渐强阶段） */
bool alarm_is_ringing(void);

/**
 * @brief  立即停止响铃（按键停铃用）
 * @note   非响铃状态调用无副作用；停铃后同一分钟内不会再次触发。
 */
void alarm_dismiss(void);

/**
 * @brief  读取当前闹钟设定（供 UI 数据快照使用）
 * @param  hour 输出：时，可为 NULL
 * @param  min  输出：分，可为 NULL
 * @param  on   输出：是否开启，可为 NULL
 */
void alarm_get(uint8_t *hour, uint8_t *min, bool *on);

#endif /* __ALARM_H */
