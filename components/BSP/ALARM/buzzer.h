#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

/* ── 硬件约定 ─────────────────────────────────────────────────────
 * 无源蜂鸣器 → GPIO18 → 三极管（8050/S8050）驱动 → 蜂鸣器
 * 无源蜂鸣器没有内部振荡电路，必须由 LEDC 输出方波才响；
 * 占空比 50% 时最响，频率偏离谐振点会明显变小声。
 *
 * ⚠ 为什么不是 GPIO32（Project_Design.md 3.2 里写的那个）：
 *   那张表是按 ESP32-WROOM-32 画的，而本板是 **ESP32-S3-N16R8**。
 *   S3 的 GPIO26~37 全部被 SPI0/1 占用：26~32 接 Flash，33~37 接八线 PSRAM。
 *   GPIO32 正是 SPID（Flash 数据线）。把它切成 GPIO 输出后，
 *   代码（XIP 在 Flash 里执行）立刻取指失败 → 死机 → TG1WDT 无限复位。
 *   实测现象：串口停在 `gpio: GPIO[1] ...`（LED 初始化）之后就再无输出。
 *
 * S3 上的可用脚（已排除 Flash/PSRAM 26~37、USB 19/20、已占用的 0/1/2/9~14/16/17/41/42）：
 *   3~8 · 15 · 18 · 21 · 22 · 38~40 · 43 · 44 · 47 · 48
 * 本工程选 18（紧邻 TVOC 的 16/17，接线顺手，无任何特殊功能）。
 */
#define BUZZER_GPIO         GPIO_NUM_18
#define BUZZER_FREQ_ALARM   2700    /* 闹钟铃声（Hz），按实物谐振点微调 */
#define BUZZER_FREQ_TICK    4000    /* 按键/提示"嘀"一声 */
#define BUZZER_DUTY_PCT     50      /* 常规鸣叫音量（占空比 %） */

/**
 * @brief  初始化蜂鸣器 PWM（LEDC，10 bit 分辨率，初始静音）
 * @note   须在调用其它 buzzer_* 接口之前调用一次；
 *         未初始化时所有接口静默返回，不会崩。
 */
void buzzer_init(void);

/**
 * @brief  立即输出指定频率与音量的方波（非阻塞）
 * @param  freq_hz   频率，0 表示静音
 * @param  duty_pct  占空比 0~100，0 表示静音，>100 按 100 处理
 * @note   任意任务可调用。内部只碰 LEDC 寄存器，不改任务状态。
 */
void buzzer_tone(uint32_t freq_hz, uint8_t duty_pct);

/**
 * @brief  停止输出（占空比归零），蜂鸣器立即静音
 * @note   若闹钟正在响铃，会连带把渐强任务一起停掉。
 */
void buzzer_stop(void);

/**
 * @brief  阻塞式"嘀"一声
 * @param  freq_hz      频率，0 用默认 BUZZER_FREQ_TICK
 * @param  duration_ms  持续时间
 * @note   会占用调用者 duration_ms 毫秒，别在 ui_task / 按键扫描里用。
 */
void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief  开始闹钟响铃：2700Hz 固定频率，占空比 20%→100% 每 500ms 递增
 * @note   已处于响铃状态时重复调用无副作用（幂等）。
 *         渐强跑满 100% 后保持最大音量，直到 buzzer_alarm_stop()。
 */
void buzzer_alarm_start(void);

/**
 * @brief  停止闹钟响铃并静音
 * @note   非响铃状态下调用无副作用。
 */
void buzzer_alarm_stop(void);

/**
 * @brief  查询是否正在响铃
 * @return true=响铃中（含渐强阶段与满音量保持阶段）
 */
bool buzzer_alarm_is_ringing(void);

#endif /* __BUZZER_H */
