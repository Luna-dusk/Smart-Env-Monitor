#include "alarm.h"
#include "buzzer.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <time.h>

static const char *TAG = "ALARM";

/* 2024-01-01 00:00:00 UTC —— 早于这个时间说明还没校时，
 * 与 sntp_time.c / lcd_ui.c 的判据保持一致（HANDOFF E7） */
#define TIME_VALID_BASELINE     1704067200

/* 影子状态：alarm 任务写，其它任务读。都是 8 位/布尔量，单次读写天然原子，
 * 不需要临界区（多字节结构体才需要）。 */
static volatile uint8_t s_hour    = ALARM_DEFAULT_HOUR;
static volatile uint8_t s_min     = ALARM_DEFAULT_MIN;
static volatile bool    s_enabled = true;
static volatile bool    s_ringing = false;

/* 本次响铃的截止时刻（esp_timer 微秒），0 表示没在响 */
static int64_t s_ring_deadline_us = 0;

/* 上次触发对应"哪一天的哪一分钟"。用它去重：
 * 一个 1s 周期任务在 07:00 这一分钟里会比对 60 次，不加去重会反复重启铃声，
 * 用户按停后下一拍又响。 */
static int64_t s_last_fire_key = -1;

/**
 * @brief  把日历时间压成"全局唯一的一分钟序号"
 * @note   年/月/日/时/分 全部参与，跨天、跨月都不会碰撞
 */
static int64_t minute_key(const struct tm *t)
{
    return (((int64_t)(t->tm_year + 1900) * 13 + (t->tm_mon + 1)) * 32 + t->tm_mday) * 1440
           + (int64_t)t->tm_hour * 60 + t->tm_min;
}

/**
 * @brief  闹钟调度任务：1s 一轮
 *
 * 两件事：
 *   1. 时间有效 + 闹钟开启 + 命中时分 + 本分钟还没触发过 → 起铃
 *   2. 响了超过 ALARM_RING_MAX_MS 还没人按停 → 静音
 */
static void alarm_task(void *arg)
{
    (void)arg;

    for (;;) {
        time_t now = time(NULL);

        if (now > TIME_VALID_BASELINE) {
            struct tm tmv;
            if (localtime_r(&now, &tmv) != NULL) {
                bool hit = s_enabled &&
                           tmv.tm_hour == (int)s_hour &&
                           tmv.tm_min  == (int)s_min;
                int64_t key = minute_key(&tmv);

                if (hit && key != s_last_fire_key) {
                    s_last_fire_key    = key;
                    s_ring_deadline_us = esp_timer_get_time() +
                                         (int64_t)ALARM_RING_MAX_MS * 1000;
                    s_ringing = true;
                    buzzer_alarm_start();
                    ESP_LOGI(TAG, "闹钟触发 %02u:%02u", (unsigned)s_hour, (unsigned)s_min);
                }
            }
        }

        if (s_ringing && esp_timer_get_time() > s_ring_deadline_us) {
            alarm_dismiss();
            ESP_LOGW(TAG, "响铃 %d 秒无人处理，自动静音", ALARM_RING_MAX_MS / 1000);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void alarm_init(void)
{
    if (xTaskCreate(alarm_task, "alarm", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create alarm task failed, 闹钟不可用");
        return;
    }
    ESP_LOGI(TAG, "闹钟就绪 %02u:%02u (%s)，响铃上限 %ds",
             (unsigned)s_hour, (unsigned)s_min,
             s_enabled ? "开启" : "关闭", ALARM_RING_MAX_MS / 1000);
}

void alarm_set(uint8_t hour, uint8_t min)
{
    if (hour > 23 || min > 59) {
        ESP_LOGW(TAG, "非法时间 %u:%u，忽略", (unsigned)hour, (unsigned)min);
        return;
    }
    s_hour = hour;
    s_min  = min;
    /* 改完时间允许在同一分钟内重新触发：比如刚设成"当前这一分钟" */
    s_last_fire_key = -1;
    ESP_LOGI(TAG, "闹钟时间改为 %02u:%02u", (unsigned)hour, (unsigned)min);
}

void alarm_set_enabled(bool on)
{
    s_enabled = on;
    /* 关闭时顺手停铃，否则正在响的铃会一直响到 60s 上限 */
    if (!on && s_ringing) alarm_dismiss();
    ESP_LOGI(TAG, "闹钟%s", on ? "开启" : "关闭");
}

bool alarm_is_enabled(void)
{
    return s_enabled;
}

bool alarm_is_ringing(void)
{
    return s_ringing;
}

void alarm_dismiss(void)
{
    s_ringing          = false;
    s_ring_deadline_us = 0;
    buzzer_alarm_stop();
}

void alarm_get(uint8_t *hour, uint8_t *min, bool *on)
{
    if (hour) *hour = s_hour;
    if (min)  *min  = s_min;
    if (on)   *on   = s_enabled;
}
