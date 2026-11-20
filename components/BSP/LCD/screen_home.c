#include "screen_home.h"
#include "ui_theme.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "SCREEN_HOME";
#define BAR_H            20
#define CLOCK_TIME_Y     16
#define CLOCK_DATE_Y     68

#define CARD_TEMP_Y      103
#define CARD_TEMP_H      42
#define CARD_AIR_Y       149
#define CARD_AIR_H       86
#define CARD_WX_Y        239
#define CARD_WX_H        43
#define CARD_BAR_Y       286
#define CARD_BAR_H       34

/* 空气卡四行（相对卡顶）：标题 14 + 单位 14 + 数值 27 + 色条行 31 = 86
 * 单位必须独占一行 —— 和标题并排时「HCHO ug/m3」要 69px，三列加起来超过
 * 216 的卡宽，ppm 会被挤出卡片外（上板实测过）。 */
#define AIR_TITLE_Y      0
#define AIR_VALUE_Y      14     /* 数值紧跟在标题下方 */
#define AIR_UNIT_Y       41     /* 单位放在数值下面（41~55），不并排 */
#define AIR_BAR_Y        66     /* 色条本身的 y；等级文字的行首在 55 */
#define AIR_LEVEL_Y      55

/* 色条：10 格，格宽 10 / 间距 2 → 总宽 118，右侧留给等级文字 */
#define AIR_SEG          10
#define AIR_SEG_W        10
#define AIR_SEG_GAP      2
#define AIR_SEG_H        8

/* 空气三列起始 x（卡内绝对坐标，卡宽 216） */
static const int AIR_COL_X[3] = { 8, 74, 140 };

/* 单位文案：cn_12 只含 ASCII，所以这里必须写 "ug/m3" 而不是 "µg/m³"
 * —— µ(U+00B5) 和 ³(U+00B3) 不在 cn_12 的 83 个字符里，会显示成空白。
 * 16px 的中文字库倒是认这两个符号，但 16px 的 "µg/m³" 宽 45px，
 * 和 24px 的数值并排会撑破 68px 的列宽（这就是上一版单位"跑到右下角"的原因）。 */
#define AIR_UNIT_UGM3    "ug/m3"
#define AIR_UNIT_PPM     "ppm"

/* ══════════════════════════════════════════════════════════════════
 * 状态
 * ══════════════════════════════════════════════════════════════════ */

static lv_obj_t *s_scr;
static lv_obj_t *s_icon_wifi;
static lv_obj_t *s_obj_bell_dot;   /* 铃铛右上角橙点：闹钟开启时出现 */
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_date;
/* 温湿度卡 */
static lv_obj_t *s_lbl_temp_val;
static lv_obj_t *s_lbl_humi_val;

/* 空气卡：3 列 × (标题 + 数值 + 单位) + 色条 + 等级文字 */
static lv_obj_t *s_lbl_air_val[3];
static lv_obj_t *s_bar[AIR_SEG];
static lv_obj_t *s_lbl_air_level;

/* 天气卡 */
static lv_obj_t *s_wx_icon;        /* 32×24 透明容器，装随天气变化的图标 */
static ui_wx_kind_t s_wx_kind = UI_WX_UNKNOWN;
static lv_obj_t *s_lbl_wx_cond;
static lv_obj_t *s_lbl_wx_temp;
static lv_obj_t *s_lbl_wx_range;

/* 底栏 */
static lv_obj_t *s_lbl_alarm_time;
static lv_obj_t *s_obj_alarm_on;
static lv_obj_t *s_icon_check;
static lv_obj_t *s_lbl_sntp;

/* 影子数据：业务任务写，lv_timer 读。
 * 结构体拷贝不是原子操作，理论上可能读到"半新半旧"的一帧 —— 下一帧
 * （≤500ms）就自愈，仪表盘场景可以接受。 */
static ui_dashboard_data_t s_data;
static volatile bool        s_dirty;

static char s_clock[8] = "--:--";
static char s_date_shown[32] = "";

/* ══════════════════════════════════════════════════════════════════
 * 小工具
 * ══════════════════════════════════════════════════════════════════ */

/** @brief 建 label（只设字体和颜色，位置由调用方 align） */
static lv_obj_t *mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *font,
                          lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_pad_all(l, 0, 0);     /* 别让 label 自带内边距顶偏基线 */
    return l;
}

/** @brief 单行卡里的"左侧对齐"快捷方式：x 相对卡内左边界，垂直自动居中 */
static lv_obj_t *mk_left(lv_obj_t *parent, const char *txt, const lv_font_t *font,
                         lv_color_t color, int x)
{
    lv_obj_t *l = mk_label(parent, txt, font, color);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
    return l;
}

/**
 * @brief  只在文本真的变化时才 set_text
 * @note   lv_label_set_text 每次都会重排 + 触发重绘；500ms 刷一次不加判断会一直重绘。
 */
static void set_text_if_changed(lv_obj_t *lbl, const char *txt)
{
    if (lbl == NULL || txt == NULL) return;
    const char *cur = lv_label_get_text(lbl);
    if (cur != NULL && strcmp(cur, txt) == 0) return;
    lv_label_set_text(lbl, txt);
}

/**
 * @brief  TVOC 浓度 → 空气质量等级（同时参考 eCO2）
 * @note   分档来自 Project_Design.md 4.4：<200 优 / <500 良 / <1000 轻度 / ≥1000 重度
 */
static ui_air_level_t air_level_of(uint16_t tvoc, uint16_t eco2)
{
    if (eco2 >= 1500 || tvoc >= 1000) return UI_AIR_HEAVY;
    if (tvoc >= 500)                  return UI_AIR_LIGHT;
    if (tvoc >= 200)                  return UI_AIR_GOOD;
    return UI_AIR_EXCELLENT;
}

/** @brief 等级 → 文案 / 颜色 / 点亮格数 */
static uint8_t air_style_of(ui_air_level_t lvl, const char **txt, lv_color_t *col)
{
    switch (lvl) {
    case UI_AIR_EXCELLENT:
        *txt = "优";
        *col = UI_COL_OK;
        return 2;
    case UI_AIR_GOOD:
        *txt = "良";
        *col = lv_color_hex(0x9ed36a);
        return 4;
    case UI_AIR_LIGHT:
        *txt = "轻度污染";
        *col = UI_COL_WARN;
        return 7;
    case UI_AIR_HEAVY:
    default:
        *txt = "重度污染";
        *col = lv_color_hex(0xe24b4a);
        return AIR_SEG;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * 各分区创建
 * ══════════════════════════════════════════════════════════════════ */

/** @brief 状态栏 y0~20：WiFi / 闹钟 / 电池，压在背景上不画卡片
 *
 * ⚠ 这里必须用 TOP_LEFT / TOP_RIGHT，**不能用 LEFT_MID / RIGHT_MID**：
 *   状态栏的 parent 是整块屏幕，"MID" 指的是屏幕的垂直中心（y≈160），
 *   用 MID 会把图标丢到空气卡那一行去（上板实测：WiFi 和电池跑到了屏幕中间）。
 *   MID 只能用在卡片内部 —— 那里的 parent 高度就是卡片高度。
 */
static void create_status_bar(void)
{
    /* 右上角成组：WiFi 在左、电池在右，两者右边缘分别距屏右 31 / 8
     * （WiFi 15px + 间距 6 + 电池 17px + 边距 8 = 46，所以 WiFi 用 -31） */
    s_icon_wifi = ui_icon_wifi(s_scr, 0, 0, false);
    lv_obj_align(s_icon_wifi, LV_ALIGN_TOP_RIGHT, -31, 3);

    lv_obj_t *bell = ui_icon_bell(s_scr, 0, 0, lv_color_hex(0xe8c76a));
    lv_obj_align(bell, LV_ALIGN_TOP_MID, -14, 2);

    /* 铃铛右上角的橙点：闹钟开启时亮，关闭时隐藏（refresh_cb 里联动） */
    s_obj_bell_dot = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_obj_bell_dot);
    lv_obj_set_size(s_obj_bell_dot, 4, 4);
    lv_obj_set_style_radius(s_obj_bell_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_obj_bell_dot, UI_COL_WARN2, 0);
    lv_obj_set_style_bg_opa(s_obj_bell_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_obj_bell_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(s_obj_bell_dot, bell, LV_ALIGN_OUT_TOP_RIGHT, -6, 2);

    lv_obj_t *bat = ui_icon_battery(s_scr, 0, 0);
    lv_obj_align(bat, LV_ALIGN_TOP_RIGHT, -8, 4);
}

/** @brief 时钟区：48px 时间 + 16px 日期，两行共 52+31 = 83 */
static void create_clock(void)
{
    s_lbl_time = mk_label(s_scr, "--:--", UI_FONT_TIME, UI_COL_TEXT);
    lv_obj_set_width(s_lbl_time, UI_HOR_RES);
    lv_obj_set_style_text_align(s_lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_time, 0, CLOCK_TIME_Y);      /* 行高 52 → 16~68 */

    s_lbl_date = mk_label(s_scr, "----年--月--日", UI_FONT_CN, UI_COL_TEXT);
    lv_obj_set_width(s_lbl_date, UI_HOR_RES);
    lv_obj_set_style_text_align(s_lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_lbl_date, 0, CLOCK_DATE_Y);      /* 行高 31 → 68~99 */
}

/**
 * @brief 温湿度卡 y109 h39 —— 单行，垂直居中
 *
 * 为什么单行：卡高 48 时"28px 数值 + 16px 中文标签"需要 30+31=61px，
 * 怎么做都会裁掉标签下半截。改成一行横排后，行高取 max(27,31)=31，
 * 卡高 39 就够，而且不再有裁切。
 */
static void create_temp_humi_card(void)
{
    lv_obj_t *c = glass_card(s_scr, CARD_TEMP_Y, CARD_TEMP_H);

    /* 温度计球心在 y=25（卡中线 21 偏下 4px）：完全居中显得偏高，
     * 再往下又太低，取中间值 */
    ui_icon_thermo(c, 5, 7);
    s_lbl_temp_val = mk_left(c, "--", UI_FONT_NUM_M, UI_COL_TEXT, 18);
    /* °C 整体用 16px（和右侧 "18~27°C" 完全同一套排版）。
     * 试过把 ° 和 C 拆开、只放大 C 到 20px，但两字的比例和间距跟右侧对不上，
     * 放大看很明显不是一套；也试过自绘空心圆当 °，笔画比字体粗。都放弃了。
     * 单位比数值小一号本来就是正常的层级关系。 */
    mk_left(c, "°C", UI_FONT_CN, UI_COL_TEXT, 51);
    mk_left(c, "温度", UI_FONT_CN, UI_COL_TEXT_DIM, 74);

    ui_divider(c, 106, 5, 1, 31);

    ui_icon_drop(c, 108, 13);
    /* 湿度值 x=120 是按**最宽数据**算出来的，不是按占位符 "--%"：
     *   "--%" 40px / "45%" 53px / "99%" 53px
     *   120 + 53 = 173 < 176("湿度" 的 x) ✓
     * 放到 126 的话 "45%" 就会到 179，压到"湿度"上。
     * 湿度真到 100%（68px）仍会挤 2px，室内基本不会出现，先不管。 */
    s_lbl_humi_val = mk_left(c, "--%", UI_FONT_NUM_M, UI_COL_TEXT, 120);
    mk_left(c, "湿度", UI_FONT_CN, UI_COL_TEXT_DIM, 176);
}

/**
 * @brief 空气卡 y152 h90 —— 三行：标题(14) + 数值(27) + 色条行(31)
 *
 * 标题和单位都用 cn_12（行高 14）。它们是纯拉丁，cn_12 的 ASCII 覆盖完整；
 * 用 16px 中文字体的话，"µg/m³" 宽 45px 会和 24px 数值一起撑破 68px 的列宽。
 */
static void create_air_card(void)
{
    static const char *TITLE[3] = { "TVOC", "HCHO", "eCO2" };
    static const char *UNIT[3]  = { AIR_UNIT_UGM3, AIR_UNIT_UGM3, AIR_UNIT_PPM };

    lv_obj_t *c = glass_card(s_scr, CARD_AIR_Y, CARD_AIR_H);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *t = mk_label(c, TITLE[i], UI_FONT_CN_XS, lv_color_hex(0xcdd8e4));
        lv_obj_set_pos(t, AIR_COL_X[i], AIR_TITLE_Y);

        /* 单位独占一行（在标题下方），不和数值并排 —— 并排会撑破列宽把
         * 最后一列的 ppm 挤出卡片外 */
        lv_obj_t *u = mk_label(c, UNIT[i], UI_FONT_CN_XS, UI_COL_TEXT_DIM);
        lv_obj_set_pos(u, AIR_COL_X[i], AIR_UNIT_Y);

        s_lbl_air_val[i] = mk_label(c, "--", UI_FONT_NUM_M, UI_COL_TEXT);
        lv_obj_set_pos(s_lbl_air_val[i], AIR_COL_X[i], AIR_VALUE_Y);
    }

    /* 色条：10 格等宽，未点亮是 18% 白 */
    for (int i = 0; i < AIR_SEG; i++) {
        lv_obj_t *seg = lv_obj_create(c);
        lv_obj_remove_style_all(seg);
        lv_obj_set_size(seg, AIR_SEG_W, AIR_SEG_H);
        lv_obj_set_pos(seg, AIR_COL_X[0] + i * (AIR_SEG_W + AIR_SEG_GAP), AIR_BAR_Y);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_set_style_bg_color(seg, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(seg, 46, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_bar[i] = seg;
    }

    s_lbl_air_level = mk_label(c, "轻度污染", UI_FONT_CN, UI_COL_WARN);
    lv_obj_set_pos(s_lbl_air_level, 136, AIR_LEVEL_Y);
}

/**
 * @brief 天气卡 y246 h35 —— 单行：左「云 + 多云 + 25°C」，右「18~27°C」
 *
 * 城市没有上屏：240 宽单行里，右侧要放"18~27°C"和"广州"两段就必然和左侧
 * 的"多云 25°C"重叠。数据字段保留在 ui_dashboard_data_t.city 里，
 * 等以后天气卡能占两行再显示。
 */
static void create_weather_card(void)
{
    lv_obj_t *c = glass_card(s_scr, CARD_WX_Y, CARD_WX_H);

    /* 天气图标：放进一个 32×24 的透明容器，便于整体重画（lv_obj_clean）。
     * 不要像以前那样直接往卡里画几何块 —— 那样换天气时没法回收旧图形。
     * 容器尺寸与坐标跟原来那朵云一致（card 内 8,7），cond 文字仍在 x=42。 */
    s_wx_icon = lv_obj_create(c);
    lv_obj_remove_style_all(s_wx_icon);
    lv_obj_set_size(s_wx_icon, 32, 24);
    lv_obj_set_pos(s_wx_icon, 8, 7);
    lv_obj_clear_flag(s_wx_icon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_wx_kind = UI_WX_UNKNOWN;
    ui_icon_weather(s_wx_icon, 0, 0, s_wx_kind);

    s_lbl_wx_cond = mk_left(c, "--", UI_FONT_CN, UI_COL_TEXT, 42);
    s_lbl_wx_temp = mk_left(c, "--", UI_FONT_NUM_M, UI_COL_TEXT, 76);
    mk_left(c, "°C", UI_FONT_CN, UI_COL_TEXT, 110);

    s_lbl_wx_range = mk_label(c, "--~--°C", UI_FONT_CN, UI_COL_TEXT);
    lv_obj_align(s_lbl_wx_range, LV_ALIGN_RIGHT_MID, -8, 0);
}

/** @brief 底栏 y285 h35 —— 单行：铃铛 07:00 ON │ ✓ SNTP 已同步 */
static void create_bottom_bar(void)
{
    lv_obj_t *c = glass_card(s_scr, CARD_BAR_Y, CARD_BAR_H);

    ui_icon_bell(c, 6, 11, UI_COL_TEXT);
    /* 用 20px：24px 时 "07:00" 占 69px，右侧的 ON 徽标和 "SNTP 已同步"
     * 就放不下（实测会挤到卡片外） */
    s_lbl_alarm_time = mk_left(c, "--:--", UI_FONT_NUM_S, UI_COL_TEXT, 20);

    /* ON 徽标：绿底圆角胶囊，闹钟关闭时整体隐藏 */
    s_obj_alarm_on = lv_obj_create(c);
    lv_obj_remove_style_all(s_obj_alarm_on);
    lv_obj_set_size(s_obj_alarm_on, 22, 16);
    lv_obj_set_style_radius(s_obj_alarm_on, 8, 0);
    lv_obj_set_style_bg_color(s_obj_alarm_on, lv_color_hex(0x1f6f4a), 0);
    lv_obj_set_style_bg_opa(s_obj_alarm_on, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_obj_alarm_on, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_obj_alarm_on, LV_ALIGN_LEFT_MID, 80, 0);

    lv_obj_t *on_txt = mk_label(s_obj_alarm_on, "ON", UI_FONT_SYM, UI_COL_OK);
    lv_obj_center(on_txt);

    ui_divider(c, 106, 2, 1, 30);

    s_icon_check = ui_icon_check(c, 0, 0);
    lv_obj_align(s_icon_check, LV_ALIGN_LEFT_MID, 114, 0);

    /* "SNTP" 用 cn_12（纯拉丁，行高 14），"已同步" 用 cn_16（中文，行高 31）。
     * 拆成两个 label 是为了把总宽精确控制在 216 卡宽内 ——
     * 一个 label 显示整串需要 88px，末端"步"字会被卡片圆角裁掉。 */
    mk_left(c, "SNTP", UI_FONT_CN_XS, UI_COL_TEXT, 128);
    s_lbl_sntp = mk_left(c, "已同步", UI_FONT_CN, UI_COL_TEXT, 161);
}

/* ══════════════════════════════════════════════════════════════════
 * 刷新
 * ══════════════════════════════════════════════════════════════════ */

/** @brief 日期行：只在跨天/校时成功时改控件 */
static void refresh_date(void)
{
    char buf[32];
    time_t now = time(NULL);
    struct tm tmv;

    /* 2024-01-01 之后才算校时过（与 lcd_ui.c 的 clock_tick 同口径） */
    if (now > 1704067200 && localtime_r(&now, &tmv) != NULL) {
        static const char *WD[7] = { "日", "一", "二", "三", "四", "五", "六" };
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d 星期%s",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 WD[tmv.tm_wday % 7]);
    } else {
        snprintf(buf, sizeof(buf), "----年--月--日");
    }

    if (strcmp(buf, s_date_shown) != 0) {
        snprintf(s_date_shown, sizeof(s_date_shown), "%s", buf);
        set_text_if_changed(s_lbl_date, buf);
    }
}

/**
 * @brief 500ms 刷新回调：把影子数据搬上屏
 *
 * 只在 lcd_ui 任务的 lv_timer_handler() 里被调用 —— LVGL 非线程安全，
 * 这是本模块唯一碰 lv_* 的地方（screen_home_set_clock 除外）。
 */
static void refresh_cb(lv_timer_t *t)
{
    (void)t;

    refresh_date();

    if (!s_dirty) return;
    s_dirty = false;

    char buf[32];
    const ui_dashboard_data_t *d = &s_data;

    /* ── 状态栏 ── */
    lv_obj_set_style_text_color(s_icon_wifi,
                                d->wifi_ok ? UI_COL_TEXT : UI_COL_TEXT_DIM, 0);

    /* ── 温湿度 ── */
    if (d->dht_valid) {
        snprintf(buf, sizeof(buf), "%d", (int)d->temp_c);
        set_text_if_changed(s_lbl_temp_val, buf);
        lv_obj_set_style_text_color(s_lbl_temp_val, UI_COL_TEXT, 0);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)d->humi_rh);
        set_text_if_changed(s_lbl_humi_val, buf);
    } else {
        set_text_if_changed(s_lbl_temp_val, "--");
        set_text_if_changed(s_lbl_humi_val, "--%");
        lv_obj_set_style_text_color(s_lbl_temp_val, UI_COL_TEXT_DIM, 0);
    }

    /* ── 空气质量 ── */
    if (d->air_valid) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)d->tvoc_ugm3);
        set_text_if_changed(s_lbl_air_val[0], buf);
        snprintf(buf, sizeof(buf), "%u", (unsigned)d->hcho_ugm3);
        set_text_if_changed(s_lbl_air_val[1], buf);
        snprintf(buf, sizeof(buf), "%u", (unsigned)d->eco2_ppm);
        set_text_if_changed(s_lbl_air_val[2], buf);
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_text_color(s_lbl_air_val[i], UI_COL_TEXT, 0);
        }

        const char *txt;
        lv_color_t  col;
        uint8_t lit = air_style_of(air_level_of(d->tvoc_ugm3, d->eco2_ppm), &txt, &col);

        set_text_if_changed(s_lbl_air_level, txt);
        lv_obj_set_style_text_color(s_lbl_air_level, col, 0);

        for (int i = 0; i < AIR_SEG; i++) {
            if (i < lit) {
                lv_obj_set_style_bg_color(s_bar[i], col, 0);
                lv_obj_set_style_bg_opa(s_bar[i], LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_color(s_bar[i], lv_color_white(), 0);
                lv_obj_set_style_bg_opa(s_bar[i], 46, 0);
            }
        }
    } else {
        for (int i = 0; i < 3; i++) {
            set_text_if_changed(s_lbl_air_val[i], "--");
            lv_obj_set_style_text_color(s_lbl_air_val[i], UI_COL_TEXT_DIM, 0);
        }
        set_text_if_changed(s_lbl_air_level, "无数据");
        lv_obj_set_style_text_color(s_lbl_air_level, UI_COL_TEXT_DIM, 0);
    }

    /* ── 天气 ── */
    if (d->weather_valid) {
        set_text_if_changed(s_lbl_wx_cond, d->cond_text);
        snprintf(buf, sizeof(buf), "%d", d->temp_now);
        set_text_if_changed(s_lbl_wx_temp, buf);
        snprintf(buf, sizeof(buf), "%d~%d°C", d->temp_min, d->temp_max);
        set_text_if_changed(s_lbl_wx_range, buf);
    } else {
        set_text_if_changed(s_lbl_wx_cond, "--");
        set_text_if_changed(s_lbl_wx_temp, "--");
        set_text_if_changed(s_lbl_wx_range, "--~--°C");
    }

    /* 天气图标：只在「类型真的变了」时才清空重画。
     * 本函数 500ms 跑一次，无脑重建会反复 malloc/free LVGL 内存池（C9 / ui.md）。
     * 晴→多云→雨 这种变化才会命中，一天也就几次。 */
    {
        ui_wx_kind_t kind = d->weather_valid ? ui_wx_kind_from_text(d->cond_text)
                                            : UI_WX_UNKNOWN;
        if (kind != s_wx_kind && s_wx_icon != NULL) {
            s_wx_kind = kind;
            lv_obj_clean(s_wx_icon);
            ui_icon_weather(s_wx_icon, 0, 0, kind);
        }
    }

    /* ── 底栏 ──
     * 关闭状态也要显示设定时间（灰显、无 ON 徽标）：
     * 之前这里关掉就写 "--:--"，看起来永远像没实现。
     * 只有"从没设过"（00:00 且关闭）才退回占位符。 */
    if (d->alarm_on) {
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)d->alarm_hour,
                 (unsigned)d->alarm_min);
        set_text_if_changed(s_lbl_alarm_time, buf);
        lv_obj_set_style_text_color(s_lbl_alarm_time, UI_COL_TEXT, 0);
        lv_obj_clear_flag(s_obj_alarm_on, LV_OBJ_FLAG_HIDDEN);
    } else if (d->alarm_hour != 0 || d->alarm_min != 0) {
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)d->alarm_hour,
                 (unsigned)d->alarm_min);
        set_text_if_changed(s_lbl_alarm_time, buf);
        lv_obj_set_style_text_color(s_lbl_alarm_time, UI_COL_TEXT_DIM, 0);
        lv_obj_add_flag(s_obj_alarm_on, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_text_if_changed(s_lbl_alarm_time, "--:--");
        lv_obj_set_style_text_color(s_lbl_alarm_time, UI_COL_TEXT_DIM, 0);
        lv_obj_add_flag(s_obj_alarm_on, LV_OBJ_FLAG_HIDDEN);
    }

    /* 状态栏铃铛橙点跟随开关状态 */
    if (s_obj_bell_dot != NULL) {
        if (d->alarm_on) lv_obj_clear_flag(s_obj_bell_dot, LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(s_obj_bell_dot, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_set_style_text_color(s_icon_check,
                                d->sntp_ok ? UI_COL_OK : UI_COL_TEXT_DIM, 0);
    lv_obj_set_style_text_color(s_lbl_sntp,
                                d->sntp_ok ? UI_COL_TEXT : UI_COL_TEXT_DIM, 0);
    set_text_if_changed(s_lbl_sntp, d->sntp_ok ? "已同步" : "未同步");
}

/* ══════════════════════════════════════════════════════════════════
 * 对外接口
 * ══════════════════════════════════════════════════════════════════ */

void screen_home_create(void)
{
    s_scr = lv_obj_create(NULL);
    if (s_scr == NULL) {
        ESP_LOGE(TAG, "screen create failed");
        return;
    }

    /* 顺序是硬约束：底 → 柔光 → 卡片 → 文字 */
    ui_theme_create_background(s_scr);

    lv_obj_set_style_text_font(s_scr, UI_FONT_CN, 0);
    lv_obj_set_style_text_color(s_scr, UI_COL_TEXT, 0);

    create_status_bar();
    create_clock();
    create_temp_humi_card();
    create_air_card();
    create_weather_card();
    create_bottom_bar();

    lv_scr_load(s_scr);

    lv_timer_create(refresh_cb, 500, NULL);
    s_dirty = true;     /* 先把一帧全 "--" 推上去 */

    ESP_LOGI(TAG, "dashboard created (cn16 lh=31, bar=%d temp=%d air=%d wx=%d bottom=%d)",
             BAR_H, CARD_TEMP_Y, CARD_AIR_Y, CARD_WX_Y, CARD_BAR_Y);
}

void screen_home_set_clock(const char *hhmm)
{
    if (hhmm == NULL) return;
    snprintf(s_clock, sizeof(s_clock), "%s", hhmm);
    set_text_if_changed(s_lbl_time, s_clock);
}

const char *screen_home_get_clock(void)
{
    return s_clock;
}

void screen_home_set_status(lcd_status_t st)
{
    if (s_icon_wifi == NULL) return;
    if (st == LCD_STATUS_CONNECTING) {
        lv_obj_set_style_text_color(s_icon_wifi, UI_COL_TEXT_DIM, 0);
    }
}

void screen_home_set_emotion(lcd_emotion_t emo)
{
    /* 空实现：仪表盘没有表情区，保留仅为兼容 lcd_ui.c 引擎的调用点 */
    (void)emo;
}

void screen_home_update(const ui_dashboard_data_t *d)
{
    if (d == NULL) return;
    /* 先拷数据再置脏：反过来会让刷新回调读到旧值后把脏标志清掉，这次更新就丢了 */
    memcpy(&s_data, d, sizeof(s_data));
    s_dirty = true;
}
