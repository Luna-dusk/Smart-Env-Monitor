#include "ui_theme.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "UI_THEME";

/**
 * @brief  画一团柔光：圆形对象 + 大阴影（LVGL 没有径向渐变）
 * @param  scr  背景 screen
 * @param  x,y  圆心左上角坐标（允许为负 —— 故意探出屏幕边缘）
 * @param  d    直径
 * @param  c    圆与阴影同色
 * @param  opa  圆本身不透明度（阴影固定 OPA_50）
 *
 * 对象本身不可点击、不可滚动，创建后不再移动、不做动画：
 * 背景一旦动起来，LVGL 就要逐帧全屏重绘，S3 上帧率和功耗都吃不消。
 */
static void glow(lv_obj_t *scr, int x, int y, int d, lv_color_t c, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_shadow_color(o, c, 0);
    lv_obj_set_style_shadow_width(o, 40, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_50, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief  造一个纯色几何块（图标拼装用，也用于分隔线）
 *
 * 全部样式清空后重设，避免 lv_obj_create 默认的 padding / border 干扰尺寸。
 */
static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h,
                       lv_color_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/**
 * @brief  造一个 LV_SYMBOL 图标 label
 * @note   符号字形在 Montserrat 字体里，所以必须显式设 UI_FONT_SYM，
 *         否则会去翻 16px 中文字库（里面没有这些私有区码点）显示成空白。
 */
static lv_obj_t *symbol(lv_obj_t *parent, const char *sym, int x, int y, lv_color_t c)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, UI_FONT_SYM, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

void ui_theme_init(void)
{
    lv_disp_t *d = lv_disp_get_default();
    if (d == NULL) {
        ESP_LOGE(TAG, "no default display, theme not applied");
        return;
    }

    /* basic 主题：不给控件预置边框/阴影/补间。仪表盘的玻璃四件套全部显式写，
     * 用 default 主题会和它的默认阴影叠加成"双阴影"，边缘发脏。 */
    lv_disp_set_theme(d, lv_theme_basic_init(d));
}

void ui_theme_create_background(lv_obj_t *scr)
{
    if (scr == NULL) return;

    /* 清掉默认样式，否则 lv_obj_create 自带的 padding 会把卡片坐标顶偏 */
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, UI_COL_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(scr, UI_COL_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* 三团柔光：坐标照抄设计稿，左上/右上/右下，都是一半在屏外 */
    glow(scr, -30, -50, 130, lv_color_hex(0x3a7ca5), LV_OPA_30);   /* 左上 · 钢蓝 */
    glow(scr, 160, -30, 110, lv_color_hex(0x2e8b9a), LV_OPA_30);   /* 右上 · 青蓝 */
    glow(scr, 140, 200, 120, lv_color_hex(0x274b73), LV_OPA_40);   /* 右下 · 深蓝 */
}

lv_obj_t *glass_card(lv_obj_t *parent, int y, int h)
{
    lv_obj_t *c = lv_obj_create(parent);

    /* 先全清再设：lv_obj_create 默认 margin/pad/border 会让 216×h 的实际内容区缩水 */
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, UI_CARD_W, h);
    lv_obj_set_pos(c, UI_CARD_X, y);

    /* ① 半透明白底 —— 玻璃感主要来自这里，能透出背后的海雾渐变和柔光 */
    lv_obj_set_style_bg_color(c, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(c, 66, 0);

    /* ② 顶部高光：靠渐变本身实现，不要再叠一个高光对象 */
    lv_obj_set_style_bg_grad_color(c, UI_COL_GLASS_BOT, 0);
    lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, 0);

    /* ③ 1px 亮边 —— 勾出卡片轮廓，比阴影重要 */
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, lv_color_white(), 0);
    lv_obj_set_style_border_opa(c, 72, 0);

    /* ④ 一层浅阴影，把卡片从背景上轻轻托起来。
     * 宽度 6 / 下偏 2：原来 10/4 会让阴影往卡底再探出 8px，
     * 而卡与卡的间隙只有 4px，视觉上就成了"两张卡叠在一起"。 */
    lv_obj_set_style_shadow_width(c, 6, 0);
    lv_obj_set_style_shadow_ofs_y(c, 2, 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_20, 0);

    lv_obj_set_style_radius(c, UI_CARD_RADIUS, 0);

    /* 内边距归零：screen_home 里所有子元素都用「卡内绝对坐标」定位
     * （第一列从 x=6 起、右侧留 8~12px），卡片自己再留 padding 会让坐标
     * 整体偏移 8px，非常容易算漏。设计稿的"呼吸感"由子元素坐标实现，
     * 不再依赖 pad。 */
    lv_obj_set_style_pad_all(c, 0, 0);

    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *ui_divider(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = block(parent, x, y, w, h, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, 46, 0);     /* 约 18%，透出玻璃底 */
    return o;
}

/* ══════════════════════════════════════════════════════════════════
 * 占位图标
 * ══════════════════════════════════════════════════════════════════ */

lv_obj_t *ui_icon_wifi(lv_obj_t *parent, int x, int y, bool connected)
{
    return symbol(parent, LV_SYMBOL_WIFI, x, y,
                  connected ? UI_COL_TEXT : UI_COL_TEXT_DIM);
}

lv_obj_t *ui_icon_battery(lv_obj_t *parent, int x, int y)
{
    return symbol(parent, LV_SYMBOL_BATTERY_3, x, y, UI_COL_OK);
}

lv_obj_t *ui_icon_bell(lv_obj_t *parent, int x, int y, lv_color_t color)
{
    return symbol(parent, LV_SYMBOL_BELL, x, y, color);
}

lv_obj_t *ui_icon_thermo(lv_obj_t *parent, int x, int y)
{
    /* 管身 6×16 + 球头 10×10，底部圆心对齐管身中线 */
    block(parent, x + 2, y,     6, 16, UI_COL_THERMO, 3);
    block(parent, x,     y + 13, 10, 10, UI_COL_THERMO, LV_RADIUS_CIRCLE);
    return NULL;
}

lv_obj_t *ui_icon_drop(lv_obj_t *parent, int x, int y)
{
    return symbol(parent, LV_SYMBOL_TINT, x, y, UI_COL_DROP);
}

lv_obj_t *ui_icon_pin(lv_obj_t *parent, int x, int y)
{
    return symbol(parent, LV_SYMBOL_GPS, x, y, UI_COL_TEXT_DIM);
}

lv_obj_t *ui_icon_check(lv_obj_t *parent, int x, int y)
{
    return symbol(parent, LV_SYMBOL_OK, x, y, UI_COL_OK);
}

/* ══════════════════════════════════════════════════════════════════
 * 天气图标（随天气状况变化）
 * ══════════════════════════════════════════════════════════════════ */

/* 图标色板：能少一层样式就少一层，全部直接写在 block() 的入参里 */
#define WX_SUN_COL      lv_color_hex(0xffd166)   /* 太阳 */
#define WX_CLOUD_COL    lv_color_hex(0xcfe0ef)   /* 主云 */
#define WX_CLOUD_DIM    lv_color_hex(0x8fa6ba)   /* 阴天的后层云 */
#define WX_RAIN_COL     lv_color_hex(0x7fb3d5)   /* 雨/雹 */
#define WX_BOLT_COL     lv_color_hex(0xf4d35e)   /* 闪电 */
#define WX_SNOW_COL     lv_color_hex(0xeaf4ff)   /* 雪花 */
#define WX_FOG_COL      lv_color_hex(0xb9c9d6)   /* 雾线 */

/** @brief 云形：三圆 + 底条，宽 28 / 高 20
 * 比旧的那朵占位云矮 2px：雨/雪/雹的颗粒需要在云底**外面**留出 3~4px，
 * 否则 32×24 里颗粒会被云自己沾掉一半。 */
static void wx_cloud(lv_obj_t *p, int x, int y, lv_color_t c)
{
    block(p, x,      y + 6,  17, 13, c, LV_RADIUS_CIRCLE);
    block(p, x + 6,  y,      17, 17, c, LV_RADIUS_CIRCLE);
    block(p, x + 14, y + 5,  14, 13, c, LV_RADIUS_CIRCLE);
    block(p, x + 2,  y + 12, 22, 6,  c, 3);
}

ui_wx_kind_t ui_wx_kind_from_text(const char *cond_text)
{
    if (cond_text == NULL || cond_text[0] == '\0') return UI_WX_UNKNOWN;

    /* 顺序不能调：「雨雪」含雨、「雷雨」含雷，见头文件注释 */
    if (strstr(cond_text, "雷") != NULL)                  return UI_WX_THUNDER;
    if (strstr(cond_text, "雪") != NULL)                  return UI_WX_SNOW;
    if (strstr(cond_text, "雹") != NULL)                  return UI_WX_HAIL;
    if (strstr(cond_text, "雨") != NULL)                  return UI_WX_RAIN;
    if (strstr(cond_text, "晴") != NULL)                  return UI_WX_SUN;
    if (strstr(cond_text, "云") != NULL)                  return UI_WX_PARTLY;
    if (strstr(cond_text, "阴") != NULL)                  return UI_WX_CLOUD;
    if (strstr(cond_text, "雾") != NULL ||
        strstr(cond_text, "霾") != NULL ||
        strstr(cond_text, "沙") != NULL ||
        strstr(cond_text, "尘") != NULL ||
        strstr(cond_text, "风") != NULL)                  return UI_WX_FOG;
    return UI_WX_UNKNOWN;
}

lv_obj_t *ui_icon_weather(lv_obj_t *parent, int x, int y, ui_wx_kind_t kind)
{
    if (parent == NULL) return NULL;

    /* 所有形状都画在 32×24 的虚拟盒子里（越界也画，LVGL 不剪裁子对象，
     * 但 screen_home 给的容器就是 32×24，所以这里自己守纪律） */
    switch (kind) {
    case UI_WX_SUN:                       /* 晴：黄圆 + 8 条短射线 */
        block(parent, x + 10, y + 6,  12, 12, WX_SUN_COL, LV_RADIUS_CIRCLE);
        block(parent, x + 15, y,       2,  4, WX_SUN_COL, 1);   /* 上 */
        block(parent, x + 15, y + 20,  2,  4, WX_SUN_COL, 1);   /* 下 */
        block(parent, x + 4,  y + 11,  4,  2, WX_SUN_COL, 1);   /* 左 */
        block(parent, x + 24, y + 11,  4,  2, WX_SUN_COL, 1);   /* 右 */
        block(parent, x + 6,  y + 1,   3,  3, WX_SUN_COL, 0);   /* 左上 */
        block(parent, x + 23, y + 1,   3,  3, WX_SUN_COL, 0);   /* 右上 */
        block(parent, x + 6,  y + 20,  3,  3, WX_SUN_COL, 0);   /* 左下 */
        block(parent, x + 23, y + 20,  3,  3, WX_SUN_COL, 0);   /* 右下 */
        break;

    case UI_WX_PARTLY:                    /* 多云：太阳在右上，云压住下半 */
        block(parent, x + 17, y, 13, 13, WX_SUN_COL, LV_RADIUS_CIRCLE);
        wx_cloud(parent, x, y + 4, WX_CLOUD_COL);
        break;

    case UI_WX_CLOUD:                     /* 阴：后层云右上错开，前层压暗 */
        wx_cloud(parent, x + 6, y - 1, WX_CLOUD_DIM);
        wx_cloud(parent, x, y + 4, WX_CLOUD_COL);
        break;

    case UI_WX_RAIN:                      /* 雨：云下四滴 */
        wx_cloud(parent, x, y, WX_CLOUD_COL);
        block(parent, x + 5,  y + 20, 2, 4, WX_RAIN_COL, 1);
        block(parent, x + 11, y + 20, 2, 4, WX_RAIN_COL, 1);
        block(parent, x + 17, y + 20, 2, 4, WX_RAIN_COL, 1);
        block(parent, x + 23, y + 20, 2, 4, WX_RAIN_COL, 1);
        break;

    case UI_WX_THUNDER:                   /* 雷雨：云 + 左下走的阶梯闪电（块间留缝） */
        wx_cloud(parent, x, y, WX_CLOUD_COL);
        block(parent, x + 17, y + 13, 6, 3, WX_BOLT_COL, 0);
        block(parent, x + 13, y + 17, 6, 3, WX_BOLT_COL, 0);
        block(parent, x + 17, y + 21, 6, 3, WX_BOLT_COL, 0);
        break;

    case UI_WX_SNOW:                      /* 雪：云下三粒白点 */
        wx_cloud(parent, x, y, WX_CLOUD_COL);
        block(parent, x + 7,  y + 20, 3, 3, WX_SNOW_COL, LV_RADIUS_CIRCLE);
        block(parent, x + 14, y + 20, 3, 3, WX_SNOW_COL, LV_RADIUS_CIRCLE);
        block(parent, x + 21, y + 20, 3, 3, WX_SNOW_COL, LV_RADIUS_CIRCLE);
        break;

    case UI_WX_HAIL:                      /* 冰雹：云下三颗蓝冰粒 */
        wx_cloud(parent, x, y, WX_CLOUD_COL);
        block(parent, x + 6,  y + 20, 4, 4, WX_RAIN_COL, LV_RADIUS_CIRCLE);
        block(parent, x + 14, y + 20, 4, 4, WX_RAIN_COL, LV_RADIUS_CIRCLE);
        block(parent, x + 22, y + 20, 4, 4, WX_RAIN_COL, LV_RADIUS_CIRCLE);
        break;

    case UI_WX_FOG:                       /* 雾/沙暴/大风：四条错落横线 */
        block(parent, x + 2,  y + 4,  20, 3, WX_FOG_COL, 2);
        block(parent, x + 9,  y + 10, 21, 3, WX_FOG_COL, 2);
        block(parent, x + 3,  y + 16, 17, 3, WX_FOG_COL, 2);
        block(parent, x + 12, y + 21, 16, 3, WX_FOG_COL, 2);
        break;

    case UI_WX_UNKNOWN:
    default:
        wx_cloud(parent, x, y + 2, WX_CLOUD_COL);
        break;
    }
    return lv_obj_get_child(parent, -1);
}
