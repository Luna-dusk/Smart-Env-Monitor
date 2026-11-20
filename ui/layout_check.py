#!/usr/bin/env python3
"""EnvDesk 仪表盘布局校验器

用途：改 screen_home.c / dashboard_preview.html 之前，先跑这个脚本。
它会
  1) 按 LVGL 的实测行高把整屏渲染成 PNG（ui/preview_check.png）
  2) 检查每个元素是否越出卡片边界、是否与同卡元素重叠
  3) 打印所有元素的矩形，方便和 C 代码里的坐标对照

为什么需要它：240px 宽的屏幕上，"533" + "ug/m3" 到底占多少像素、
三列加起来会不会超 216，靠肉眼估必然出错（上一版 ppm 就是这样跑到卡外的）。

用法：python3 ui/layout_check.py
"""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

# ── 屏幕与缩放 ────────────────────────────────────────────────────
W, H = 240, 320
S = 3                      # 输出放大 3 倍，方便看细节
OUT = Path(__file__).resolve().parent / "preview_check.png"

# ── LVGL 实测行高（grep .line_height 得到，必须与 ui_theme.h 一致）──
LH_CN, LH_CN12, LH_N24, LH_N48 = 31, 14, 27, 52


def font_file(pattern: str) -> str:
    return subprocess.check_output(["fc-match", "-f", "%{file}", pattern]).decode().strip()


CN_TTC = font_file("Noto Sans CJK SC")
NUM_TTF = font_file("DejaVu Sans")


def font(path: str, size: int):
    return ImageFont.truetype(path, size * S)


F_CN = font(CN_TTC, 16)
F_CN12 = font(CN_TTC, 12)
F_N20 = font(NUM_TTF, 20)
F_N24 = font(NUM_TTF, 24)
F_N48 = font(NUM_TTF, 48)


def measure(text: str, f, px: int = 0) -> int:
    """返回文字在原尺寸下的像素宽度（不含额外间距 px）"""
    if not text:
        return px
    w = f.getlength(text) / S
    return int(round(w + px))


# ══════════════════════════════════════════════════════════════════
# 布局定义 —— 这里的每个数字都要和 screen_home.c 一致
#
# 垂直预算（合计 320）：
#   状态栏  0~ 20 (20)
#   时间   16~ 68 (52)   ← 与状态栏盒子重叠 4px，但字形不碰
#   日期   68~ 99 (31)
#   温湿度 103~145 (42)
#   空气   149~235 (86)
#   天气   239~282 (43)
#   底栏   286~320 (34)
# ══════════════════════════════════════════════════════════════════
CARD_X, CARD_W = 12, 216

CARDS = [
    ("温湿度", 103, 42),
    ("空气",   149, 86),
    ("天气",   239, 43),
    ("底栏",   286, 34),
]

TEMP_Y, TEMP_H = 103, 42
AIR_Y,  AIR_H  = 149, 86
WX_Y,   WX_H   = 239, 43
BAR_Y,  BAR_H  = 286, 34

WHITE = (255, 255, 255)
DIM = (185, 201, 218)
OKC = (76, 195, 138)
WARN = (255, 182, 72)
WARN2 = (255, 138, 61)

# 元素表：(卡片名, 元素名, x, y, w, h, 文本, 字体, 颜色)
# y 是相对卡片顶部；w/h 是实际占位
E = []


def add(card, name, x, y, txt, f, col, h, w=None, w_text=None):
    # w_text: 校验宽度时用的"最宽实际数据"，默认同 txt。
    # 占位符 "--" 只有 17px，而 "45%" 有 53px —— 按占位符排必然漏检。
    ref = w_text if w_text is not None else txt
    w = measure(ref, f) if w is None else w
    E.append(dict(card=card, name=name, x=x, y=y, w=w, h=h, txt=txt, f=f, col=col))


# ── 温湿度卡（h42，中线 21）──
# 度号用「自绘空心圆 + 20px 的 C」，不用 cn_16 的 "°C"：
#   · Montserrat 只含 ASCII(32~126)，没有 ° (176)
#   · 中文字体只有 16px 一档，°C 整体只有 16px 高，比 24px 的数值小太多
#   圆圈 6px + C 14px，视觉高度接近 20px，和数值更协调
add("温湿度", "温度计管", 7,  7, "", F_CN, WARN2, 15, 6)
add("温湿度", "温度计球", 5, 20, "", F_CN, WARN2, 10, 10)
add("温湿度", "温度值", 18, 7, "25", F_N24, WHITE, LH_N24, w_text="-5")
add("温湿度", "温度单位", 51, 5, "°C", F_CN, WHITE, LH_CN)
add("温湿度", "温度标签", 74, 5, "温度", F_CN, DIM, LH_CN)
add("温湿度", "分隔线", 106, 5, "", F_CN, (255, 255, 255), 31, 1)
add("温湿度", "水滴", 108, 13, "", F_CN, (90, 176, 224), 16, 12)
add("温湿度", "湿度值", 120, 7, "45%", F_N24, WHITE, LH_N24, w_text="99%")
add("温湿度", "湿度标签", 176, 5, "湿度", F_CN, DIM, LH_CN)

# ── 空气卡（h86）── 四行：标题 / 数值 / 单位 / 色条
# 单位放在**数值下方**：和数值并排时「HCHO ug/m3」要 69px，三列加起来超过
# 216 的卡宽，ppm 会被挤出卡片外；放在数值下面既不挤，读起来也是
# "533 ← 这个数，单位是 ug/m3" 的顺序
AIR_COLX = (8, 74, 140)
TITLES = ("TVOC", "HCHO", "eCO2")
UNITS = ("ug/m3", "ug/m3", "ppm")
VALS = ("533", "83", "933")

for i, cx in enumerate(AIR_COLX):
    add("空气", f"标题{i}", cx, 0, TITLES[i], F_CN12, (205, 216, 228), LH_CN12)
    add("空气", f"数值{i}", cx, 14, VALS[i], F_N24, WHITE, LH_N24)
    add("空气", f"单位{i}", cx, 41, UNITS[i], F_CN12, DIM, LH_CN12)

for i in range(10):
    add("空气", f"色条{i}", 8 + i * 12, 66, "", F_CN, WARN if i < 7 else (255, 255, 255), 8, 10)
add("空气", "等级文字", 136, 55, "轻度污染", F_CN, WARN, LH_CN)

# ── 天气卡（h43，中线 21）──
add("天气", "太阳", 27, 7, "", F_CN, (255, 209, 102), 13, 13)
add("天气", "云1", 10, 17, "", F_CN, (207, 224, 239), 13, 17)
add("天气", "云2", 16, 11, "", F_CN, (207, 224, 239), 17, 17)
add("天气", "云3", 24, 16, "", F_CN, (207, 224, 239), 13, 14)
add("天气", "云4", 12, 23, "", F_CN, (207, 224, 239), 6, 22)
add("天气", "天气文字", 42, 6, "多云", F_CN, WHITE, LH_CN)
add("天气", "天气温度", 76, 8, "25", F_N24, WHITE, LH_N24, w_text="-5")
add("天气", "天气单位", 110, 6, "°C", F_CN, WHITE, LH_CN)
add("天气", "温度范围", CARD_W - 8 - measure("18~27°C", F_CN), 6, "18~27°C", F_CN, WHITE, LH_CN)

# ── 底栏（h34，中线 17）──
# "07:00" 用 20px 而不是 24px：24px 时这一串占 69px，右侧的 ON 徽标和
# "SNTP 已同步" 就放不下了（实测会挤到卡外）。20px 只要 58px，全部塞得进。
add("底栏", "铃铛", 6, 11, "", F_CN, WHITE, 13, 12)
add("底栏", "闹钟时间", 20, 4, "07:00", F_N20, WHITE, 23, w_text="00:00")
add("底栏", "ON徽标", 80, 9, "", F_CN, (31, 111, 74), 16, 22)
add("底栏", "ON文字", 86, 11, "ON", F_CN12, OKC, LH_CN12)
add("底栏", "分隔线", 106, 2, "", F_CN, (255, 255, 255), 30, 1)
add("底栏", "SNTP", 128, 10, "SNTP", F_CN12, WHITE, LH_CN12)
add("底栏", "已同步", 161, 2, "已同步", F_CN, WHITE, LH_CN)


# ══════════════════════════════════════════════════════════════════
# 几何校验
# ══════════════════════════════════════════════════════════════════
def card_h(name):
    return dict((c[0], c[2]) for c in CARDS)[name]


def check():
    issues = []
    for e in E:
        ch = card_h(e["card"])
        right = e["x"] + e["w"]
        # 文字/图形整体不能在卡外
        if e["x"] < 0:
            issues.append(f"[{e['card']}] {e['name']} 左边越界 x={e['x']}")
        if right > CARD_W:
            issues.append(f"[{e['card']}] {e['name']} 右边越界 right={right} > {CARD_W}（超出 {right - CARD_W}px）")
        if e["y"] < 0:
            issues.append(f"[{e['card']}] {e['name']} 顶部越界 y={e['y']}")
        if e["y"] + e["h"] > ch:
            issues.append(f"[{e['card']}] {e['name']} 底部越界 bottom={e['y'] + e['h']} > {ch}（超出 {e['y'] + e['h'] - ch}px）")

    # 同卡内"有文字的元素"两两重叠检查（纯图形如色条/云不参与）
    def rect(e):
        return (e["x"], e["y"], e["x"] + e["w"], e["y"] + e["h"])

    def inter(a, b):
        ax1, ay1, ax2, ay2 = rect(a)
        bx1, by1, bx2, by2 = rect(b)
        return not (ax2 <= bx1 or bx2 <= ax1 or ay2 <= by1 or by2 <= ay1)

    texts = [e for e in E if e["txt"]]
    for i in range(len(texts)):
        for j in range(i + 1, len(texts)):
            a, b = texts[i], texts[j]
            if a["card"] != b["card"]:
                continue
            if inter(a, b):
                issues.append(f"[{a['card']}] 「{a['txt']}」与「{b['txt']}」重叠 "
                              f"({a['x']}~{a['x']+a['w']} vs {b['x']}~{b['x']+b['w']})")
    return issues


# ══════════════════════════════════════════════════════════════════
# 渲染
# ══════════════════════════════════════════════════════════════════
def lerp(c1, c2, t):
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))


def draw_bell(d, x, y, w, h, col):
    """画一个铃铛形状（圆顶 + 直身 + 底沿 + 铃舌）。
    直接画矩形的话和 LVGL 里的 LV_SYMBOL_BELL 差太远，看不出是不是铃铛。"""
    X, Y, Wd, Hd = x * S, y * S, w * S, h * S
    d.ellipse([X + Wd * .18, Y, X + Wd * .82, Y + Hd * .60], fill=col)          # 圆顶
    d.rectangle([X + Wd * .18, Y + Hd * .28, X + Wd * .82, Y + Hd * .70], fill=col)
    d.rounded_rectangle([X, Y + Hd * .64, X + Wd, Y + Hd * .78], radius=Hd * .07, fill=col)
    d.ellipse([X + Wd * .40, Y + Hd * .80, X + Wd * .60, Y + Hd], fill=col)     # 铃舌


def render():
    """全程按 S 倍尺寸绘制（坐标和字体都乘 S），不做二次缩放。
    早先的坑：字体建了 3 倍、坐标却用 1 倍，画出来字全部溢出——布局校验器
    本身也必须"所见即所得"，否则它校验的和看到的不是同一个东西。"""
    img = Image.new("RGBA", (W * S, H * S))
    d = ImageDraw.Draw(img)

    # 背景三段渐变
    stops = [(0.0, (0x1d, 0x32, 0x4e)), (0.52, (0x13, 0x25, 0x3c)), (1.0, (0x0d, 0x1b, 0x2d))]
    for yy in range(H * S):
        t = yy / (H * S - 1)
        for k in range(len(stops) - 1):
            t0, c0 = stops[k]
            t1, c1 = stops[k + 1]
            if t0 <= t <= t1:
                img.paste(lerp(c0, c1, (t - t0) / (t1 - t0)), (0, yy, W * S, yy + 1))
                break

    # 三团柔光
    for (gx, gy, gd, col, opa) in [(-30, -50, 130, (58, 124, 165), .30),
                                   (160, -30, 110, (46, 139, 154), .30),
                                   (140, 200, 120, (39, 75, 115), .40)]:
        layer = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        X, Y, D = gx * S, gy * S, gd * S
        ImageDraw.Draw(layer).ellipse([X, Y, X + D, Y + D], fill=col + (int(255 * opa),))
        layer = layer.filter(ImageFilter.GaussianBlur(D * 0.28))
        img = Image.alpha_composite(img, layer)

    # 玻璃卡
    for name, cy, chh in CARDS:
        cw, ch = CARD_W * S, chh * S
        card = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
        cd = ImageDraw.Draw(card)
        for y in range(ch):
            t = y / max(ch - 1, 1)
            r, g, b = lerp((255, 255, 255), (223, 233, 242), t)
            cd.line([(0, y), (cw, y)], fill=(r, g, b, 66))
        mask = Image.new("L", (cw, ch), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, cw - 1, ch - 1], radius=14 * S, fill=255)
        card.putalpha(Image.composite(card.getchannel("A"), Image.new("L", (cw, ch), 0), mask))
        img.alpha_composite(card, (CARD_X * S, cy * S))

        bd = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
        ImageDraw.Draw(bd).rounded_rectangle(
            [CARD_X * S, cy * S, (CARD_X + CARD_W) * S - 1, (cy + chh) * S - 1],
            radius=14 * S, outline=(255, 255, 255, 78), width=S)
        img = Image.alpha_composite(img, bd)

    d = ImageDraw.Draw(img)

    # 顶栏图标（不画卡片）
    # WiFi：右上角，右边距 31（电池 17px + 间距 6 的左侧）
    d.arc([194 * S, 5 * S, 209 * S, 20 * S], 210, 330, fill=WHITE, width=2 * S)
    d.arc([197 * S, 9 * S, 206 * S, 18 * S], 210, 330, fill=WHITE, width=2 * S)
    d.ellipse([199.5 * S, 15 * S, 203.5 * S, 19 * S], fill=WHITE)
    draw_bell(d, 113, 2, 14, 15, (232, 199, 106))
    d.ellipse([123 * S, 2 * S, 127 * S, 6 * S], fill=WARN2)
    # 电池：右上角，右边距 8
    d.rounded_rectangle([215 * S, 5 * S, 232 * S, 16 * S], radius=2 * S, outline=OKC, width=S)
    d.rectangle([217 * S, 7 * S, 227 * S, 14 * S], fill=OKC)
    d.rectangle([232 * S, 9 * S, 234 * S, 13 * S], fill=OKC)

    # 时间 / 日期
    d.text((W * S // 2, (16 + LH_N48 // 2) * S), "22:22", font=F_N48, fill=WHITE, anchor="mm")
    d.text((W * S // 2, (68 + LH_CN // 2) * S), "2026-11-18 星期三", font=F_CN, fill=WHITE, anchor="mm")

    # 各卡内元素
    card_y = dict((c[0], c[1]) for c in CARDS)
    for e in E:
        ax = (CARD_X + e["x"]) * S
        ay = (card_y[e["card"]] + e["y"]) * S
        if e["txt"]:
            d.text((ax, ay + e["h"] * S / 2), e["txt"], font=e["f"], fill=e["col"], anchor="lm")
        else:
            x2, y2 = ax + e["w"] * S, ay + e["h"] * S
            if e["name"] in ("温度计球", "水滴"):
                d.ellipse([ax, ay, x2, y2], fill=e["col"])
            elif e["name"] == "铃铛":
                draw_bell(d, e["x"] + CARD_X, card_y[e["card"]] + e["y"], e["w"], e["h"], e["col"])
            elif e["w"] >= e["h"]:
                d.rounded_rectangle([ax, ay, x2, y2],
                                    radius=min(e["h"] * S // 2, 4 * S), fill=e["col"])
            else:
                d.rectangle([ax, ay, x2, y2], fill=e["col"])

    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").save(OUT)
    return OUT


if __name__ == "__main__":
    p = render()
    issues = check()

    print(f"渲染完成: {p}  ({W*S}x{H*S})")
    print(f"\n元素总数: {len(E)}")
    print("\n── 垂直预算 ──")
    print(f"  状态栏  0~ 20    时间 16~ 68    日期 68~ 99")
    print(f"  温湿度 {TEMP_Y}~{TEMP_Y+TEMP_H}    空气 {AIR_Y}~{AIR_Y+AIR_H}    "
          f"天气 {WX_Y}~{WX_Y+WX_H}    底栏 {BAR_Y}~{BAR_Y+BAR_H}")

    if issues:
        print(f"\n✗ 发现 {len(issues)} 个问题：")
        for s in issues:
            print("  ·", s)
        sys.exit(1)
    print("\n✓ 几何校验通过：无越界、无文字重叠")
