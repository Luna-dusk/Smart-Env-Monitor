#include "buzzer.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

/* LEDC 资源分配：低俗模式 + TIMER0 + CHANNEL0（与 LCD 背光若同用 LEDC 需错开通道） */
#define BUZZER_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_TIMER     LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BUZZER_DUTY_BITS      LEDC_TIMER_10_BIT
#define BUZZER_DUTY_MAX       1023      /* 10 bit 满量程 */

/* 闹钟渐强参数：20% 起步，每 500ms 加 20%，约 2.5s 达到最大音量 */
#define ALARM_DUTY_START      204
#define ALARM_DUTY_STEP       205
#define ALARM_FADE_STEP_MS    500

static bool          s_inited  = false;
static volatile bool s_ringing = false;
static TaskHandle_t  s_alarm_task = NULL;

/**
 * @brief  占空比百分比 → 10 bit 原始值
 * @param  pct 0~100，超过 100 按 100 处理
 */
static uint32_t duty_from_pct(uint8_t pct)
{
    if (pct > 100) pct = 100;
    return (uint32_t)pct * BUZZER_DUTY_MAX / 100;
}

/**
 * @brief  闹钟渐强任务
 * @param  arg 未使用
 *
 * 从 20% 占空比开始，每 500ms 递增一档；跑满 100% 后不再改占空比，
 * 只空转等待，直到 buzzer_alarm_stop() 把本任务删除。
 *
 * @note 任务永不主动退出——这样 s_alarm_task 句柄始终有效，
 *       stop 时直接 vTaskDelete 不会有"删已退出任务"的风险。
 */
static void alarm_fade_task(void *arg)
{
    (void)arg;

    uint32_t duty = ALARM_DUTY_START;
    for (;;) {
        if (duty <= BUZZER_DUTY_MAX) {
            ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
            ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
            duty += ALARM_DUTY_STEP;
        }
        vTaskDelay(pdMS_TO_TICKS(ALARM_FADE_STEP_MS));
    }
}

void buzzer_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .timer_num       = BUZZER_LEDC_TIMER,
        .duty_resolution = BUZZER_DUTY_BITS,
        .freq_hz         = BUZZER_FREQ_ALARM,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&tcfg) != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer config failed, 蜂鸣器不可用");
        return;
    }

    ledc_channel_config_t ccfg = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .duty       = 0,            /* 上电静音 */
        .hpoint     = 0,
    };
    if (ledc_channel_config(&ccfg) != ESP_OK) {
        ESP_LOGE(TAG, "ledc channel config failed, 蜂鸣器不可用");
        return;
    }

    s_inited = true;
    ESP_LOGI(TAG, "buzzer ready GPIO%d %dHz", (int)BUZZER_GPIO, BUZZER_FREQ_ALARM);
}

void buzzer_tone(uint32_t freq_hz, uint8_t duty_pct)
{
    if (!s_inited) return;

    if (freq_hz == 0 || duty_pct == 0) {
        buzzer_stop();
        return;
    }

    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty_from_pct(duty_pct));
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

/**
 * @brief  内部使用：停掉渐强任务（不碰占空比）
 *
 * 只做任务删除与标志清理，静音由调用者负责，避免重复写 LEDC。
 */
static void alarm_task_kill(void)
{
    if (s_alarm_task != NULL) {
        vTaskDelete(s_alarm_task);
        s_alarm_task = NULL;
    }
    s_ringing = false;
}

void buzzer_stop(void)
{
    if (!s_inited) return;

    /* 闹钟还在跑的话先把渐强任务停掉，否则它下一拍又把占空比写回去 */
    if (s_ringing) alarm_task_kill();

    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_inited) return;

    buzzer_tone(freq_hz ? freq_hz : BUZZER_FREQ_TICK, BUZZER_DUTY_PCT);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    buzzer_stop();
}

void buzzer_alarm_start(void)
{
    if (!s_inited || s_ringing) return;

    /* 先把频率锁到谐振点，之后渐强任务只改占空比 */
    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, BUZZER_FREQ_ALARM);

    s_ringing = true;
    if (xTaskCreate(alarm_fade_task, "buzz_alarm", 2048, NULL, 5, &s_alarm_task) != pdPASS) {
        ESP_LOGE(TAG, "create alarm fade task failed");
        s_ringing    = false;
        s_alarm_task = NULL;
    }
}

void buzzer_alarm_stop(void)
{
    if (!s_inited) return;

    alarm_task_kill();

    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

bool buzzer_alarm_is_ringing(void)
{
    return s_ringing;
}
