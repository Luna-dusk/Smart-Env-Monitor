#include "sntp_time.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include <stdlib.h>
#include <time.h>

static const char *TAG = "SNTP";

/* 2024-01-01 00:00:00 UTC —— 早于这个时间说明还没校过时 */
#define TIME_VALID_BASELINE   1704067200

static volatile bool s_synced = false;

/**
 * @brief SNTP 同步完成回调（在 lwip 的 sntp 任务上下文里执行）
 * @note  这里只置标志，不做任何耗时操作。
 */
static void on_time_synced(struct timeval *tv)
{
    (void)tv;
    s_synced = true;

    time_t now = time(NULL);
    struct tm tmv;
    if (localtime_r(&now, &tmv) != NULL) {
        ESP_LOGI(TAG, "已对时: %04d-%02d-%02d %02d:%02d:%02d (CST)",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }
}

void sntp_time_start(void)
{
    /* 时区必须在这里设一次：localtime() 靠 TZ 环境变量，
     * 不设的话拿到的是 UTC，显示的钟点会差 8 小时。*/
    setenv("TZ", "CST-8", 1);
    tzset();

    /* 幂等保护：重复 init 会让 lwip 起两个 sntp 任务 */
    if (esp_sntp_getoperatingmode() != ESP_SNTP_OPMODE_POLL) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    }
    esp_sntp_setservername(0, "ntp.aliyun.com");   /* 国内首选，延迟低 */
    esp_sntp_setservername(1, "pool.ntp.org");     /* 备用 */
    esp_sntp_set_time_sync_notification_cb(on_time_synced);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP 已启动: ntp.aliyun.com / pool.ntp.org");
}

bool sntp_time_is_synced(void)
{
    return s_synced && (time(NULL) > TIME_VALID_BASELINE);
}
