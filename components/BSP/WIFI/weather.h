#ifndef __WEATHER_H
#define __WEATHER_H

/**
 * @file  weather.h
 * @brief 天气预报客户端（和风天气 QWeather · 新版 API）
 *
 * 两个请求拼出一张天气卡：
 *   GET /weather/v1/current/{lat}/{lon}          → 实况：天气现象 + 当前温度
 *   GET /weather/v1/daily/{lat}/{lon}?days=1     → 今日：最高温 / 最低温
 */

#include <stdint.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════════
 * 凭据与地点 —— 换成你自己的再编译
 *
 * 1. console.qweather.com 注册 → 应用管理 → 创建项目 → 得到 API KEY
 * 2. 同站「设置」里复制你的 API Host（不带 https://）
 * 3. 填到下面两行，重新烧录
 *
 * 下面保留了明显的占位符特征（含 xxxx / your-），
 * weather_init() 会据此判断"还没配置"，直接不启动任务，
 * 界面上天气卡保持 "--"，不会发出无效请求。
 * ══════════════════════════════════════════════════════════════════ */
#define WEATHER_API_HOST    "p43yfvb4rv.re.qweatherapi.com"
#define WEATHER_API_KEY     "85bdf30c80d84ec494a4055c8827c53b"

/* 观测点：湘潭市区。新版 API 只认经纬度（最多两位小数），不返回城市名，
 * 所以城市文案单独给一个宏 —— 目前没上屏位置，留给以后天气卡占两行时用。
 * 坐标取自湘潭市行政范围（东经 111°58′~113°05′，北纬 27°21′~28°05′）的中心。 */
#define WEATHER_LAT         "27.83"
#define WEATHER_LON         "112.94"
#define WEATHER_CITY_NAME   "湘潭"

/* 轮询节奏：成功 30min 一次；失败按 1min → 5min → 15min 退避 */
#define WEATHER_PERIOD_MS   (30 * 60 * 1000)

/** 天气快照：全部字段都是"最后一次成功获取"的值，失败不会清空 */
typedef struct {
    char     cond_text[16];   /* "多云"，已按字库和卡宽规范化到 ≤2 个汉字 */
    int      temp_now;
    int      temp_min;
    int      temp_max;
    char     city[24];
    bool     valid;
    uint32_t fetch_epoch;     /* 上次成功时间（time_t），失败时用来判断数据有多旧 */
} weather_data_t;

/**
 * @brief  启动天气任务（只调用一次，位置在 WiFi 初始化之后即可）
 * @note   任务自己等 WiFi 连上，不需要外部触发；未填凭据时直接返回。
 */
void weather_init(void);

/**
 * @brief  取一份快照（任意任务可调，非阻塞）
 * @param  out 输出结构体，内部整体拷贝
 * @return true = 有可用数据（至少成功拉取过一次）
 * @note   结构体拷贝不是原子操作，可能读到半新半旧的一帧；
 *         UI 500ms 刷一次，下一帧就自愈，仪表盘场景可以接受。
 */
bool weather_get(weather_data_t *out);

#endif /* __WEATHER_H */
