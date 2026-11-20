#ifndef __TVOC301_H
#define __TVOC301_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/uart.h"

/* ── 硬件约定（见 Project_Design.md 3.4 端口定义）────────────────────
 * TVOC-301（广州传苣）UART 主动上报，9600-8-N-1，9 字节定长帧。
 *
 *   模组 B 脚(TXD) → GPIO16 (UART2_RX)   ★ 若模组 TXD 空闲电平为 5V，
 *                                          必须 10k/15k 分压后再接
 *   模组 A 脚(RXD) ← GPIO17 (UART2_TX)   模组主动上报，此脚实际用不到
 *
 * 选 UART2 是因为 UART1 已被 BSP/UART（GPIO41/42）占用。
 */
#define TVOC301_UART_NUM    UART_NUM_2
#define TVOC301_TX_GPIO     17
#define TVOC301_RX_GPIO     16
#define TVOC301_BAUD        9600

/** 一次有效帧解析出的三通道浓度 */
typedef struct {
    uint16_t tvoc_ugm3;   /* 总挥发性有机物，量程 0~5000 ug/m3 */
    uint16_t hcho_ugm3;   /* 甲醛，量程 0~2000 ug/m3 */
    uint16_t eco2_ppm;    /* 等效二氧化碳，量程 0~5000 ppm */
} tvoc301_data_t;

/**
 * @brief  初始化 UART2 与帧解析任务
 * @note   驱动只装一次；重复调用会产生第二个解析任务，请只在 app_main 调一次。
 *         任务优先级 6（高于 sensor/ui，低于音频类），栈 3KB。
 */
void tvoc301_init(void);

/**
 * @brief  取最近一帧有效数据（非阻塞，读取不消费）
 * @param  out 输出缓冲，不可为 NULL
 * @return true=拿到过快照；false=模块未初始化或上电至今还没收到合法帧
 * @note   任意任务可调用。底层是深度 1 的 mailbox 队列（Peek），
 *         拿到的一定是当前最新值，不需要消费者清队。
 * @note   拿到 true 不代表数据新鲜，配合 tvoc301_age_ms() 判断。
 */
bool tvoc301_latest(tvoc301_data_t *out);

/**
 * @brief  距最近一帧有效数据的毫秒数
 * @return 毫秒数；从未收到合法帧时返回 UINT32_MAX
 * @note   UI 侧建议 >5000ms 就显示 "--" 并灰显（模组约 1s 上报一次）。
 */
uint32_t tvoc301_age_ms(void);

#endif /* __TVOC301_H */
