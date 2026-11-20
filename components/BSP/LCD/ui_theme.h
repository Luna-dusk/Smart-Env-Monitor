#ifndef __UI_THEME_H
#define __UI_THEME_H

/**
 * @file  ui_theme.h
 * @brief 仪表盘主题层：画布尺寸 / 色板 / 假玻璃卡片 / 占位图标
 *
 * 视觉规格来源：ui/ui.md（240×320 假玻璃版）与 ui/预览图片/ 下的三张预览图。
 * 这里只放**可复用的视觉零件**，具体布局在 screen_home.c。
 *
 * 设计稿给的是网页 CSS 参数，搬到 LVGL 后有三处必须落地修改，
 * 改因写在各处注释里，不要随手改回：
 *   ① ℃(U+2103) 不在字库里 → 统一写 "°C"（° = U+00B0，字库有）
 *   ② 40px 以上大间距按 LVGL 行高重算 —— LVGL 字体行高比 CSS line-height 高
 *   ③ 卡片内边距垂直方向从 8 压到 2，否则 48px 高的卡装不下"28px 数值 + 16px 标签"
 */

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/* ══════════════════════════════════════════════════════════════════
 * 画布
 * ══════════════════════════════════════════════════════════════════ */

/** 屏幕宽（像素）。全工程唯一真值来源，lcd_ui.c 用它算 draw buffer。 */
#define UI_HOR_RES      240
/** 屏幕高（像素）。 */
#define UI_VER_RES      320

/* 卡片几何：设计稿定死 x=12 / 宽 216，左右各留 12px 背景 */
#define UI_CARD_X       12
#define UI_CARD_W       216
#define UI_CARD_RADIUS  14

/* ══════════════════════════════════════════════════════════════════
 * 字体
 *
 * 中文只有 ui_font_cn_common_16 一个能用的：它是完整常用字库
 * （ASCII + Latin-1 + 3161 汉字）。同目录的 cn_12 / cn_20 是"小阁"
 * 语音助手时期生成的子集，只有 83 个旧界面用字，**不含**
 * "温""湿""度""云""州""污""染"等字，拿来显示新文案会变空白方框。
 *
 * 数字一律走 Montserrat：中文字体的数字是等宽全角、字宽不等，
 * 时钟每秒跳动会左右抖。
 * ══════════════════════════════════════════════════════════════════ */

extern const lv_font_t ui_font_cn_common_16;
/* lcd_ui.c 的 font_selfcheck() 会遍历可用的中文字库做压缩位图体检，
 * 这些符号必须可见。ui_font_cn_16 是 common_16 的旧名字，
 * 早先的 ui_theme.h 用别名维持兼容，这里保留同样的约定。 */
extern const lv_font_t ui_font_cn_12;
#define ui_font_cn_16   ui_font_cn_common_16

#define UI_FONT_CN      (&ui_font_cn_common_16)     /* 中文正文（行高 31） */
/* 拉丁小字专用：cn_12 行高 14，含完整 ASCII，用来排 TVOC / 单位 / SNTP 这类纯英文，
 * 别拿它显示中文 —— 它只有 83 个旧界面用字，不含"温湿云州"等。
 * cn_16 行高 31、cn_12 行高 14，这两个数字是下面所有坐标的计算基准。 */
#define UI_FONT_CN_XS   (&ui_font_cn_12)
#define UI_FONT_TIME    (&lv_font_montserrat_48)    /* 时钟 12:34，行高 52 */
#define UI_FONT_NUM_L   (&lv_font_montserrat_28)    /* 备用：行高 30 */
#define UI_FONT_NUM_M   (&lv_font_montserrat_24)    /* 数值主力：行高 27 */
#define UI_FONT_NUM_S   (&lv_font_montserrat_20)    /* 底栏闹钟 07:00：行高 22 */
#define UI_FONT_SYM     (&lv_font_montserrat_14)    /* LV_SYMBOL 图标，行高 16 */

extern const lv_font_t ui_font_cn_12;

/* ══════════════════════════════════════════════════════════════════
 * 色板（ui.md 第 28~35 行 / 第 41~51 行）
 * ══════════════════════════════════════════════════════════════════ */

#define UI_COL_BG_TOP       lv_color_hex(0x1d324e)  /* 深蓝海雾 · 顶 */
#define UI_COL_BG_BOT       lv_color_hex(0x0d1b2d)  /* 深蓝海雾 · 底 */
#define UI_COL_GLASS_BOT    lv_color_hex(0xdfe9f2)  /* 玻璃卡渐变下缘（顶部高光靠它） */
#define UI_COL_TEXT         lv_color_white()        /* 主文字 */
#define UI_COL_TEXT_DIM     lv_color_hex(0xb9c9da)  /* 副标签（"温度""湿度"） */
#define UI_COL_OK           lv_color_hex(0x4cc38a)  /* ON / 已同步 绿 */
#define UI_COL_WARN         lv_color_hex(0xffb648)  /* 空气色条 · 亮 */
#define UI_COL_WARN2        lv_color_hex(0xff8a3d)  /* 空气色条 · 渐变尾 */
#define UI_COL_THERMO       lv_color_hex(0xff8a3d)  /* 温度计图标 */
#define UI_COL_DROP         lv_color_hex(0x5ab0e0)  /* 水滴图标 */

/* ══════════════════════════════════════════════════════════════════
 * 接口
 * ══════════════════════════════════════════════════════════════════ */

/**
 * @brief 切换基础主题（basic）
 * @note  必须在创建任何 screen 之前调用一次。用 basic 而不是 default：
 *        default 会给普通对象预置边框和阴影，叠在假玻璃上变成"双阴影"，
 *        且带主题补间动画，仪表盘不需要。
 */
void ui_theme_init(void);

/**
 * @brief 铺背景：深蓝竖向渐变 + 3 团静态柔光
 * @param scr 目标 screen（一般是 lv_obj_create(NULL) 建出来的）
 *
 * 创建顺序是硬约束：先渐变底 → 再柔光 → 之后才能建卡片和文字。
 * LVGL 按对象创建顺序绘制，顺序反了玻璃下面就没有颜色可透。
 *
 * 柔光用"圆 + 大阴影"模拟径向渐变（LVGL 没有 radial gradient）。
 * 三团都故意一半探出屏幕边缘，这是设计稿要的效果，不要"修正"回屏内。
 * @note 全程静态，不做任何动画 —— 背景一动 LVGL 就要逐帧全屏重绘。
 */
void ui_theme_create_background(lv_obj_t *scr);

/**
 * @brief 造一张假玻璃卡片
 * @param parent 父对象
 * @param y      卡片顶部 y 坐标（绝对坐标，设计稿里卡与卡的间距已定死）
 * @param h      卡片高度
 * @return 卡片对象；后续内容以它为 parent，坐标相对卡内左上角
 *
 * 配方固定四件套，**不做真模糊**（真模糊要多备两帧缓冲 ≈300KB）：
 *   半透明白底 bg_opa=66 + 竖向渐变到 #dfe9f2 当顶部高光
 *   + 1px 白色亮边 border_opa=72 + 浅阴影 10px / 下偏 4px / opa 20%
 *
 * @note 内边距：水平 8 保留设计稿的呼吸感；垂直压到 2 —— 48px 高的卡
 *       要装下 28px 数值 + 16px 标签，上下各 8px 会把内容挤到卡外。
 * @note 已清除 LV_OBJ_FLAG_SCROLLABLE，卡片不可滚动。
 */
lv_obj_t *glass_card(lv_obj_t *parent, int y, int h);

/**
 * @brief 卡内分隔线（1px 半透明白）
 * @param parent 卡片对象
 * @param x      相对卡内内容区的 x
 * @param y      相对卡内内容区的 y
 * @param w      宽（竖线传 1）
 * @param h      高
 */
lv_obj_t *ui_divider(lv_obj_t *parent, int x, int y, int w, int h);

/* ── 占位图标 ────────────────────────────────────────────────────
 * 位图图标还没做，先用 LV_SYMBOL + 简单几何图形顶上。
 * 每个图标都是独立函数：以后换成图标字库或 PNG，只改对应函数体，
 * screen_home.c 一行都不用动。
 * 全部返回自建对象；用 LV_SYMBOL 的实现返回 label。
 * ──────────────────────────────────────────────────────────────── */

/** @brief WiFi 图标，connected=true 亮白，false 灰 */
lv_obj_t *ui_icon_wifi(lv_obj_t *parent, int x, int y, bool connected);
/** @brief 电池图标 */
lv_obj_t *ui_icon_battery(lv_obj_t *parent, int x, int y);
/** @brief 闹钟铃铛 */
lv_obj_t *ui_icon_bell(lv_obj_t *parent, int x, int y, lv_color_t color);
/** @brief 温度计（自绘：管身 + 球头） */
lv_obj_t *ui_icon_thermo(lv_obj_t *parent, int x, int y);
/** @brief 水滴 */
lv_obj_t *ui_icon_drop(lv_obj_t *parent, int x, int y);
/* ── 天气图标：随天气状况变化 ──────────────────────────────────
 * 和风返回的 cond 文案已经被 weather.c 的 k_cond_fix[] 规范到 ≤2 个汉字，
 * 这里按关键字判类型。画面尺寸统一 32×24（比原来那朵云宽 1px，
 * 因为云形本身占 31px），screen_home 把图标放在卡内 (8,7)。
 *
 * 刷新时**只在类型变化时重画**：每 500ms 重建控件会反复 malloc/free
 * LVGL 内存池，违背 ui.md 的「刷新不重建控件」。
 * ──────────────────────────────────────────────────────────────── */
typedef enum {
    UI_WX_UNKNOWN = 0,   /* 无数据 / 认不出的文案 → 通用云 */
    UI_WX_SUN,           /* 晴 */
    UI_WX_PARTLY,        /* 多云 / 少云：云 + 半露太阳 */
    UI_WX_CLOUD,         /* 阴：双层云 */
    UI_WX_RAIN,          /* 雨 / 阵雨 / 暴雨 */
    UI_WX_THUNDER,       /* 雷雨 */
    UI_WX_SNOW,          /* 雪 / 雨雪 */
    UI_WX_HAIL,          /* 冰雹 */
    UI_WX_FOG,           /* 雾 / 沙暴 / 风暴 / 大风 */
} ui_wx_kind_t;

/**
 * @brief  把天气文案映射成图标类型
 * @param  cond_text 已规范化的天气文案（可为 NULL）
 * @return 图标类型；空指针/空串返回 UI_WX_UNKNOWN
 *
 * 判断顺序有讲究（不要随手调）：
 *   雷 → 雪 → 雹 → 雨 → 晴 → 云 → 阴 → 雾/沙/风
 * 「雨雪」含「雨」、「雷雨」含「雨」，所以雷和雪必须排在雨前面。
 */
ui_wx_kind_t ui_wx_kind_from_text(const char *cond_text);

/**
 * @brief  画一个天气图标（全部自绘，不依赖图标字库/位图）
 * @param  parent 父对象（天气卡，或 screen_home 里那个 32×24 容器）
 * @param  x,y    左上角（相对 parent）
 * @param  kind   图标类型
 * @return 新建的图形对象（最后一个子对象），一般不需要保存
 */
lv_obj_t *ui_icon_weather(lv_obj_t *parent, int x, int y, ui_wx_kind_t kind);
/** @brief 定位针 */
lv_obj_t *ui_icon_pin(lv_obj_t *parent, int x, int y);
/** @brief 绿色对勾 */
lv_obj_t *ui_icon_check(lv_obj_t *parent, int x, int y);

#endif /* __UI_THEME_H */
