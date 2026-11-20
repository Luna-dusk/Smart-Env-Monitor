#include "tvoc301.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "TVOC301";

/* 帧格式（说明书 V00.001，9 字节定长，MSB 在前）
 *   B0  0x2C        帧头
 *   B1  0xE4        保留
 *   B2~B3  TVOC    ug/m3
 *   B4~B5  HCHO    ug/m3
 *   B6~B7  eCO2    ppm
 *   B8    校验和 = B0~B7 累加和的低 8 位
 */
#define TVOC301_HEAD1        0x2C
#define TVOC301_HEAD2        0xE4
#define TVOC301_PAYLOAD_LEN  6          /* B2~B7 */

#define TVOC301_RX_BUF       256        /* 必须大于硬件 FIFO(128B) */
#define TVOC301_EVT_QUEUE    20         /* uart_event_t 队列深度 */

/** 逐字节喂入的解析状态机 */
typedef enum {
    P_WAIT_HEAD = 0,
    P_WAIT_E4,
    P_WAIT_PAYLOAD,
    P_WAIT_SUM,
} tvoc_parse_state_t;

typedef struct {
    tvoc_parse_state_t state;
    uint8_t payload[TVOC301_PAYLOAD_LEN];
    uint8_t idx;
    uint8_t sum;                        /* uint8_t 自然回绕 = 取低 8 位 */
} tvoc_parser_t;

static tvoc_parser_t    s_parser;
static QueueHandle_t    s_frame_q     = NULL;   /* 深度 1 mailbox */
static QueueHandle_t    s_uart_evt_q  = NULL;
static volatile uint32_t s_last_frame_ms = 0;   /* 0 = 上电至今没收到过 */

/**
 * @brief  喂入一个字节，凑满合法帧时返回 true
 * @param  p    解析器状态
 * @param  b    本次收到的字节
 * @param  out  合法帧的输出（仅返回 true 时有效）
 *
 * UART 是字节流，一个事件里可能是 1.5 帧、也可能是半帧，所以必须逐字节
 * 走状态机。两个经典边界：
 *   1) 帧错位后数据里再出现 0x2C —— 当作新帧头立刻重新对齐，
 *      避免白等一整个上报周期；
 *   2) 校验失败 —— 丢帧回到找头，不回溯重放（1s 一帧，丢一帧无感知）。
 */
static bool tvoc301_feed(tvoc_parser_t *p, uint8_t b, tvoc301_data_t *out)
{
    switch (p->state) {
    case P_WAIT_HEAD:
        if (b == TVOC301_HEAD1) {
            p->sum   = b;                   /* 校验和自帧头起算 */
            p->state = P_WAIT_E4;
        }
        break;

    case P_WAIT_E4:
        if (b == TVOC301_HEAD2) {
            p->sum += b;
            p->idx   = 0;
            p->state = P_WAIT_PAYLOAD;
        } else if (b == TVOC301_HEAD1) {
            p->sum = b;                     /* 伪帧头：当前字节即新帧起点 */
        } else {
            p->state = P_WAIT_HEAD;         /* 失步，重新找头 */
        }
        break;

    case P_WAIT_PAYLOAD:
        p->payload[p->idx++] = b;
        p->sum += b;
        if (p->idx >= TVOC301_PAYLOAD_LEN) p->state = P_WAIT_SUM;
        break;

    case P_WAIT_SUM:
        p->state = P_WAIT_HEAD;
        if (p->sum == b) {
            out->tvoc_ugm3 = (uint16_t)((p->payload[0] << 8) | p->payload[1]);
            out->hcho_ugm3 = (uint16_t)((p->payload[2] << 8) | p->payload[3]);
            out->eco2_ppm  = (uint16_t)((p->payload[4] << 8) | p->payload[5]);
            return true;
        }
        break;
    }
    return false;
}

/**
 * @brief  UART2 事件任务：收字节 → 喂状态机 → 覆盖 mailbox
 * @param  arg 未使用
 *
 * 阻塞在 uart_evt_queue 上，有数据才被唤醒，空闲时不占 CPU。
 * 收到合法帧就 xQueueOverwrite 到深度 1 的队列——只保留最新值。
 *
 * @note 溢出/错误分支必须复位解析器，否则残帧会一直卡在 WAIT_PAYLOAD。
 */
static void tvoc301_event_task(void *arg)
{
    (void)arg;

    uart_event_t evt;
    /* 必须和驱动侧 RX 环形缓冲一样大：evt.size 是"环形缓冲里攒了多少字节"，
     * 上限是 TVOC301_RX_BUF(256)，不是硬件 FIFO(128)。写 128 会栈越界。 */
    uint8_t buf[TVOC301_RX_BUF];
    tvoc301_data_t frame;
    uint32_t ok_cnt = 0;

    ESP_LOGI(TAG, "parser start UART%d %d-8-N-1 RX=GPIO%d",
             TVOC301_UART_NUM, TVOC301_BAUD, TVOC301_RX_GPIO);

    for (;;) {
        if (xQueueReceive(s_uart_evt_q, &evt, portMAX_DELAY) != pdTRUE) continue;

        switch (evt.type) {
        case UART_DATA: {
            int n = uart_read_bytes(TVOC301_UART_NUM, buf, evt.size, 0);
            for (int i = 0; i < n; i++) {
                if (tvoc301_feed(&s_parser, buf[i], &frame)) {
                    xQueueOverwrite(s_frame_q, &frame);
                    s_last_frame_ms = (uint32_t)(esp_timer_get_time() / 1000);
                    /* 每秒一帧，不打日志会刷屏；每 30 帧（约 30s）报一次 */
                    if (++ok_cnt % 30 == 1) {
                        ESP_LOGI(TAG, "TVOC=%u HCHO=%u eCO2=%u",
                                 frame.tvoc_ugm3, frame.hcho_ugm3, frame.eco2_ppm);
                    }
                }
            }
            break;
        }

        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            ESP_LOGW(TAG, "UART 溢出，清缓冲并复位状态机");
            uart_flush_input(TVOC301_UART_NUM);
            xQueueReset(s_uart_evt_q);
            s_parser.state = P_WAIT_HEAD;
            break;

        case UART_PARITY_ERR:
        case UART_FRAME_ERR:
            ESP_LOGW(TAG, "线路错误(type=%d)，已忽略", evt.type);
            break;

        default:
            break;
        }
    }
}

void tvoc301_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = TVOC301_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(TVOC301_UART_NUM, TVOC301_RX_BUF, 0,
                            TVOC301_EVT_QUEUE, &s_uart_evt_q, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart driver install failed, TVOC 不可用");
        return;
    }
    ESP_ERROR_CHECK(uart_param_config(TVOC301_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(TVOC301_UART_NUM,
                                 TVOC301_TX_GPIO, TVOC301_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* 深度 1：mailbox 模式，只留最新一帧，消费端 Peek 不取走 */
    s_frame_q = xQueueCreate(1, sizeof(tvoc301_data_t));
    if (s_frame_q == NULL) {
        ESP_LOGE(TAG, "create frame queue failed");
        return;
    }
    s_parser = (tvoc_parser_t){0};

    if (xTaskCreate(tvoc301_event_task, "tvoc_uart", 3072, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create tvoc task failed");
        return;
    }

    ESP_LOGI(TAG, "init done, 等模组主动上报（约 1s/帧）");
}

bool tvoc301_latest(tvoc301_data_t *out)
{
    if (s_frame_q == NULL || out == NULL) return false;
    return xQueuePeek(s_frame_q, out, 0) == pdTRUE;
}

uint32_t tvoc301_age_ms(void)
{
    if (s_last_frame_ms == 0) return UINT32_MAX;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return now_ms - s_last_frame_ms;
}
