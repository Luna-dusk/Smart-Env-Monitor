#ifndef __SNTP_TIME_H
#define __SNTP_TIME_H

/**
 * @file  sntp_time.h
 * @brief SNTP 网络对时
 *
 * 为什么需要它：ESP32 上电时 `time()` 返回 1970（内部 RTC 从零起算），
 * **WiFi 连上并不会自动校时**。没有这一步，界面会一直显示 `--:--`、
 * 日期显示 `----年--月--日`，而且闹钟功能完全不可用（不知道现在几点）。
 */

#include <stdbool.h>

/**
 * @brief  启动 SNTP 对时（幂等，重复调用无副作用）
 * @note   内部会先设时区 `CST-8`（UTC+8，无夏令时）再起 SNTP，
 *         所以本函数之后 `localtime()` 直接得到北京时间。
 *         POLL 模式下 IDF 会按 `CONFIG_LWIP_SNTP_UPDATE_DELAY`
 *         （默认 1 小时）自动重新同步，不用自己开定时器。
 * @note   必须在 WiFi 拿到 IP 之后调用才有意义；提前调用不会报错，
 *         SNTP 会在联网后自行重试。
 */
void sntp_time_start(void);

/**
 * @brief  查询时间是否已经同步过
 * @return true = 已收到过 SNTP 应答，且当前时间晚于 2024-01-01
 * @note   加 2024 这个下限是为了挡住"回调已触发但 RTC 还没写进去"的瞬时窗口，
 *         以及与 lcd_ui.c / screen_home.c 里判断时间有效性的口径保持一致。
 */
bool sntp_time_is_synced(void);

#endif /* __SNTP_TIME_H */
