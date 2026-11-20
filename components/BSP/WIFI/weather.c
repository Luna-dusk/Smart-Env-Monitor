#include "weather.h"
#include "wifi.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "miniz.h"          /* ROM 里的 tinfl：解 gzip，不用引第三方 zlib */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "WEATHER";

#define HTTP_TIMEOUT_MS   8000
#define BODY_CAP          4096
/* TLS 握手（含 crt bundle 解析 ~100 张证书）吃栈。
 * 12KB = esp_http_client 8KB 经验值 + 证书解析 + 余量。见 G1。 */
#define WEATHER_TASK_STK  12288

/* 响应体缓冲用 static：任务栈里放 8KB 太奢侈，本任务全系统唯一实例，没有并发。
 * s_raw 装原始响应（可能是 gzip），s_body 装解压后的 JSON。 */
static char s_raw[BODY_CAP];
static char s_body[BODY_CAP];

static weather_data_t s_data;

/* ══════════════════════════════════════════════════════════════════
 * HTTP
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief  解开 gzip（RFC 1952）到 dst
 * @return 解压后字节数；不是合法 gzip 或解压失败返回 -1
 *
 * @note 为什么必须自己解：和风**无视** `Accept-Encoding: identity`，一律返回
 *       gzip（实测：请求头写了 identity，响应仍是 1F 8B 开头）。而 ESP-IDF 的
 *       esp_http_client 只在"下载到文件"时才处理 Content-Encoding。
 *
 *       手上的工具是 ROM 里的 miniz，但 tinfl 只认 raw deflate / zlib，
 *       不认 gzip 的 10 字节头（外加可能出现的 FEXTRA/FNAME/FCOMMENT/FHCRC
 *       变长字段）和尾部 8 字节（CRC32 + ISIZE）。
 *       所以这里先手工跳过头尾，把中间的纯 deflate 交给它。
 *
 *       ⚠ 绝对不要用 `tinfl_decompress_mem_to_mem()`（2026-11-19 G1 的真凶）：
 *       它内部把 `tinfl_decompressor decomp;` 放在**调用者的栈上**，
 *       而这个结构体 sizeof = **11,000 字节**（3 张 1024 项哈夫曼查找表）。
 *       本任务栈原本只有 8KB，于是一次解压就往下踩穿 3KB 堆块，
 *       相邻的 esp_tls_t / 空闲链表当场损坏，几秒后才在别处炸开。
 *       正确做法：用底层 coroutine `tinfl_decompress()`，把 11KB 放 PSRAM。
 */
static int gunzip(const uint8_t *src, int src_len, char *dst, size_t dst_cap)
{
    /* 10 字节固定头 + ≥1 字节 deflate + 8 字节尾 */
    if (src_len < 19) return -1;
    if (src[0] != 0x1f || src[1] != 0x8b || src[2] != 0x08) return -1;

    size_t off = 10;                       /* ID1 ID2 CM FLG MTIME(4) XFL OS */
    uint8_t flg = src[3];

    if (flg & 0x04) {                      /* FEXTRA：2 字节长度 + 数据 */
        if (off + 2 > (size_t)src_len) return -1;
        size_t xlen = (size_t)src[off] | ((size_t)src[off + 1] << 8);
        off += 2 + xlen;
    }
    if (flg & 0x08) {                      /* FNAME：以 0 结尾 */
        while (off < (size_t)src_len && src[off] != 0) off++;
        off++;
    }
    if (flg & 0x10) {                      /* FCOMMENT：以 0 结尾 */
        while (off < (size_t)src_len && src[off] != 0) off++;
        off++;
    }
    if (flg & 0x02) off += 2;              /* FHCRC */

    if (off + 8 > (size_t)src_len) return -1;

    size_t deflate_len = (size_t)src_len - off - 8;   /* 尾部 8 字节丢掉 */

    /* 11KB 的解压器状态放堆上（优先 PSRAM），不要留在栈里，理由见函数头 */
    tinfl_decompressor *dec = heap_caps_malloc(sizeof(*dec),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dec == NULL) {
        dec = heap_caps_malloc(sizeof(*dec),
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (dec == NULL) {
        ESP_LOGE(TAG, "解压器分配失败（%u 字节）", (unsigned)sizeof(*dec));
        return -1;
    }

    tinfl_init(dec);

    size_t in_len  = deflate_len;
    size_t out_len = dst_cap - 1;

    /* pOut_buf_start == pOut_buf_next：单块一次性解压，不留 LZ 历史窗口。
     * 必须带 TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF：dst 只有 4KB（< 32KB 字典），
     * 不带这个 flag 时 tinfl 会把输出当环形缓冲算回指距离，直接解错。
     * ROM 的 tinfl_decompress_mem_to_mem() 内部也是这么补的。 */
    tinfl_status st = tinfl_decompress(dec,
                                       src + off, &in_len,
                                       (mz_uint8 *)dst, (mz_uint8 *)dst,
                                       &out_len, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(dec);

    if (st != TINFL_STATUS_DONE) {
        if (st == TINFL_STATUS_HAS_MORE_OUTPUT) {
            ESP_LOGW(TAG, "解压输出超过 %u 字节", (unsigned)(dst_cap - 1));
        } else {
            ESP_LOGW(TAG, "解压失败 status=%d（入 %u 字节，出 %u 字节）",
                     (int)st, (unsigned)in_len, (unsigned)out_len);
        }
        return -1;
    }

    dst[out_len] = '\0';
    return (int)out_len;
}

/**
 * @brief  GET 一个 JSON 到 s_body
 * @return 响应字节数，失败返回 -1
 *
 * @note 和风默认 gzip 且不认 identity（见 gunzip 的注释），所以这里先读原始
 *       字节到 s_raw，检测 1F 8B 后再解到 s_body。解压失败直接返回错误，
 *       不会把二进制喂给 cJSON —— 那只会得到一个毫无信息量的解析失败。
 */
static int http_get(const char *url)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* 内置根证书，免手塞 CA */
        .timeout_ms        = HTTP_TIMEOUT_MS,
    };

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        ESP_LOGE(TAG, "http client init failed");
        return -1;
    }

    /* 仍然发 identity：万一服务端哪天守规范了，能省一次解压 */
    esp_http_client_set_header(cli, "Accept-Encoding", "identity");
    esp_http_client_set_header(cli, "X-QW-Api-Key", WEATHER_API_KEY);

    int ret = -1;
    int total = 0;

    do {
        if (esp_http_client_open(cli, 0) != ESP_OK) {
            ESP_LOGW(TAG, "open failed");
            break;
        }

        esp_http_client_fetch_headers(cli);
        int status = esp_http_client_get_status_code(cli);
        if (status != 200) {
            /* 401 = key 不对，403 = key 与 Host 不匹配（免费/商业 Host 不能混用） */
            ESP_LOGW(TAG, "HTTP %d", status);
            break;
        }

        while (total < (int)sizeof(s_raw) - 1) {
            int n = esp_http_client_read(cli, s_raw + total,
                                         (int)sizeof(s_raw) - 1 - total);
            if (n <= 0) break;
            total += n;
        }
        s_raw[total] = '\0';

        if (total == 0) {
            ESP_LOGW(TAG, "空响应");
            break;
        }

        if (total >= 2 && (unsigned char)s_raw[0] == 0x1f &&
                          (unsigned char)s_raw[1] == 0x8b) {
            int n = gunzip((const uint8_t *)s_raw, total, s_body, sizeof(s_body));
            if (n < 0) {
                ESP_LOGE(TAG, "gzip 解压失败（原始 %d 字节）", total);
                break;
            }
            ret = n;
        } else {
            memcpy(s_body, s_raw, total);
            s_body[total] = '\0';
            ret = total;
        }
    } while (0);

    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return ret;
}

/* ══════════════════════════════════════════════════════════════════
 * JSON
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief  取一个整数温度
 *
 * 新版 API 里温度都是 {"value": 29.94, "unit": "°C"} 这种对象，
 * 但同一份文档里日预报也出现过扁平数字，两种都认一下。四舍五入到整度。
 */
static int json_value_int(cJSON *parent, const char *key)
{
    cJSON *o = cJSON_GetObjectItem(parent, key);
    if (o == NULL) return 0;

    if (cJSON_IsObject(o)) {
        cJSON *v = cJSON_GetObjectItem(o, "value");
        return (v != NULL) ? (int)lround(cJSON_GetNumberValue(v)) : 0;
    }
    return (int)lround(cJSON_GetNumberValue(o));
}

/* ══════════════════════════════════════════════════════════════════
 * 天气文案
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *raw;     /* 和风返回的原文 */
    const char *shown;   /* 上屏文案 */
} cond_fix_t;

/**
 * @brief  天气现象文案的两道约束
 *
 * ① **卡宽**：天气卡里 cond 从 x=42 起、温度从 x=76 起，中间只有 32px，
 *    正好两个 16px 汉字。三字词（"雷阵雨""晴间多云"）会直接压到温度上。
 * ② **字库**：ui_font_cn_common_16 的 4199 个码点里**没有「霾」「飓」**，
 *    原样显示会变空白（「雹」是有的，不用换）。
 *
 * 所以长词和缺字词都在这里换成两字同义词。表里没有的词，兜底截前两个字。
 */
static const cond_fix_t k_cond_fix[] = {
    { "晴间多云",       "多云" },
    { "雷阵雨伴有冰雹", "冰雹" },
    { "雷阵雨",         "雷雨" },
    { "雨夹雪",         "雨雪" },
    { "大暴雨",         "暴雨" },
    { "特大暴雨",       "暴雨" },
    { "强沙尘暴",       "沙暴" },
    { "沙尘暴",         "沙暴" },
    { "热带风暴",       "风暴" },
    { "热带低压",       "风暴" },
    { "龙卷风",         "大风" },
    { "飓风",           "大风" },   /* 「飓」不在字库 */
    { "冻雾",           "雾" },
    { "霾",             "雾" },     /* 「霾」不在字库 */
};

static void normalize_cond(const char *raw, char *out, size_t cap)
{
    const char *txt = (raw != NULL) ? raw : "";

    for (size_t i = 0; i < sizeof(k_cond_fix) / sizeof(k_cond_fix[0]); i++) {
        if (strcmp(txt, k_cond_fix[i].raw) == 0) {
            txt = k_cond_fix[i].shown;
            break;
        }
    }

    /* 最多 6 字节 = 2 个汉字；截断时不要停在 UTF-8 字符中间 */
    size_t len = strlen(txt);
    if (len > 6) {
        len = 6;
        while (len > 0 && ((unsigned char)txt[len] & 0xC0) == 0x80) len--;
    }
    snprintf(out, cap, "%.*s", (int)len, txt);
}

/* ══════════════════════════════════════════════════════════════════
 * 两个请求
 * ══════════════════════════════════════════════════════════════════ */

/** @brief 实况：天气现象 + 当前温度 */
static bool fetch_current(weather_data_t *w)
{
    char url[160];
    snprintf(url, sizeof(url), "https://%s/weather/v1/current/%s/%s?lang=zh",
             WEATHER_API_HOST, WEATHER_LAT, WEATHER_LON);

    if (http_get(url) <= 0) return false;

    cJSON *root = cJSON_Parse(s_body);
    if (root == NULL) {
        ESP_LOGW(TAG, "current: JSON 解析失败");
        return false;
    }

    bool ok = false;
    cJSON *cond = cJSON_GetObjectItem(root, "condition");
    if (cJSON_IsObject(cond)) {
        normalize_cond(cJSON_GetStringValue(cJSON_GetObjectItem(cond, "text")),
                       w->cond_text, sizeof(w->cond_text));
        w->temp_now = json_value_int(root, "temperature");
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

/** @brief 今日预报：最高温 / 最低温 */
static bool fetch_daily(weather_data_t *w)
{
    char url[160];
    /* days=1：只要今天，7 天会让 body 涨到 10KB+ 且没有任何用处 */
    snprintf(url, sizeof(url), "https://%s/weather/v1/daily/%s/%s?days=1&lang=zh",
             WEATHER_API_HOST, WEATHER_LAT, WEATHER_LON);

    if (http_get(url) <= 0) return false;

    cJSON *root = cJSON_Parse(s_body);
    if (root == NULL) {
        ESP_LOGW(TAG, "daily: JSON 解析失败");
        return false;
    }

    bool ok = false;
    cJSON *days = cJSON_GetObjectItem(root, "days");
    cJSON *today = cJSON_GetArrayItem(days, 0);
    if (cJSON_IsObject(today)) {
        w->temp_max = json_value_int(today, "temperatureMax");
        w->temp_min = json_value_int(today, "temperatureMin");
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

/* ══════════════════════════════════════════════════════════════════
 * 任务
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief  天气轮询任务
 *
 * 节奏：
 *   · 没联网 → 5s 看一次事件组，不发请求（没意义的请求只会白烧流量）
 *   · 成功   → 30min 后再来
 *   · 失败   → 1min → 5min → 15min 退避，三次之后一直 15min
 *
 * 失败**不清空**已有数据：天气卡保留上一次的值，比突然变回 "--" 好得多。
 */
static void weather_task(void *arg)
{
    (void)arg;

    static const uint32_t BACKOFF_S[3] = { 60, 300, 900 };
    int fail = 0;

    for (;;) {
        EventGroupHandle_t ev = wifi_get_event_group();
        if (ev == NULL || (xEventGroupGetBits(ev) & WIFI_CONNECTED_BIT) == 0) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* 先在副本上拼，两个请求都成功才整体提交，避免"有温度没最高最低"的半截数据 */
        weather_data_t tmp = s_data;
        bool ok = fetch_current(&tmp) && fetch_daily(&tmp);

        if (ok) {
            tmp.valid       = true;
            tmp.fetch_epoch = (uint32_t)time(NULL);
            snprintf(tmp.city, sizeof(tmp.city), "%s", WEATHER_CITY_NAME);
            s_data = tmp;
            fail = 0;
            ESP_LOGI(TAG, "%s %d°C (%d~%d°C)", tmp.cond_text, tmp.temp_now,
                     tmp.temp_min, tmp.temp_max);
            /* 一次成功抓取（TLS + 解压）之后的峰值栈余量，接近 0 就是又要炸 */
            ESP_LOGI(TAG, "栈余量 %u B / %d B",
                     (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                     WEATHER_TASK_STK);
        } else {
            if (fail < 3) fail++;
            ESP_LOGW(TAG, "获取失败，%u 秒后重试", BACKOFF_S[fail - 1]);
        }

        uint32_t wait_s = ok ? (WEATHER_PERIOD_MS / 1000) : BACKOFF_S[fail - 1];
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));
    }
}

void weather_init(void)
{
    /* 占位符没换就别起任务：否则每 30min 发一次注定 401 的请求 */
    if (strstr(WEATHER_API_HOST, "xxxx") != NULL ||
        strstr(WEATHER_API_KEY, "your-") != NULL) {
        ESP_LOGW(TAG, "未配置 API Host / Key，天气功能跳过（天气卡保持 --）");
        return;
    }

    if (xTaskCreate(weather_task, "weather", WEATHER_TASK_STK, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create weather task failed");
        return;
    }
    ESP_LOGI(TAG, "天气任务已启动 %s,%s 每 %d 分钟一次",
             WEATHER_LAT, WEATHER_LON, WEATHER_PERIOD_MS / 60000);
}

bool weather_get(weather_data_t *out)
{
    if (out == NULL) return false;
    *out = s_data;
    return s_data.valid;
}
