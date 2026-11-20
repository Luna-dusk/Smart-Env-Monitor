"""天气图标预览：直接从 ui_theme.c 的 ui_icon_weather() 解析 block() 坐标渲染，
所以预览和固件永远同一套数字（HANDOFF C11 的教训：三处坐标必须一致）。

用法：python3 ui/icon_sheet.py   → ui/weather_icons.png（6 倍放大）+ /tmp/icons_1x*.png（真实分辨率）
依赖：Pillow、fc-match（DejaVu Sans）
"""
import re, subprocess
from PIL import Image, ImageDraw, ImageFont

SRC = open("/home/violet/esp/Smart-Env-Monitor/components/BSP/LCD/ui_theme.c").read()
fn = SRC[SRC.index("lv_obj_t *ui_icon_weather"):]
fn = fn[:fn.index("\n    return lv_obj_get_child")]
parts = re.split(r"case UI_WX_(\w+):", fn)
COL = dict(WX_SUN_COL=(0xff,0xd1,0x66), WX_CLOUD_COL=(0xcf,0xe0,0xef), WX_CLOUD_DIM=(0x8f,0xa6,0xba),
           WX_RAIN_COL=(0x7f,0xb3,0xd5), WX_BOLT_COL=(0xf4,0xd3,0x5e),
           WX_SNOW_COL=(0xea,0xf4,0xff), WX_FOG_COL=(0xb9,0xc9,0xd6))
CIRCLE = 999

def num(s):  # "x + 10" -> 10 ; "x" -> 0
    return int(s.replace(" ","").split("+")[1]) if "+" in s else 0

def parse(body):
    out = []
    for m in re.finditer(r"(block|wx_cloud)\(parent,\s*([^,]+),\s*([^,]+),\s*(.*?)\);", body, re.S):
        kind, ax, ay, rest = m.group(1), num(m.group(2)), num(m.group(3)), m.group(4)
        if kind == "wx_cloud":
            c = COL[rest.strip()]
            out += [(ax,ay+6,17,13,CIRCLE,c),(ax+6,ay,17,17,CIRCLE,c),
                    (ax+14,ay+5,14,13,CIRCLE,c),(ax+2,ay+12,22,6,3,c)]
        else:
            a = [s.strip() for s in rest.split(",")]
            w,h,r = int(a[0]), int(a[1]), a[3]
            r = CIRCLE if r.startswith("LV_RADIUS_CIRCLE") else int(r)
            out.append((ax,ay,w,h,r,COL[a[2]]))
    return out

kinds = [(parts[i], parse(parts[i+1])) for i in range(1, len(parts), 2)]
kinds = [(k, b) for k, b in kinds if k != "UNKNOWN"] + [("UNKNOWN", parse(parts[-1]))]
print("解析到图标：", [k for k,_ in kinds])

def render(S, path):
    CW,CH,gap,pad,lab = 32,24,10,6,18
    cols = 5; rows = (len(kinds)+cols-1)//cols
    W = cols*(CW+gap)+gap; H = rows*(CH+lab+gap)+gap
    img = Image.new("RGB",(W*S,H*S),(0x1a,0x2c,0x44))
    d = ImageDraw.Draw(img)
    fpath = subprocess.check_output(["fc-match","-f","%{file}","DejaVu Sans"]).decode().strip()
    f = ImageFont.truetype(fpath, 8*S)
    for i,(name,blocks) in enumerate(kinds):
        cx = (gap + (i%cols)*(CW+gap))*S; cy = (gap + (i//cols)*(CH+lab+gap))*S
        d.rounded_rectangle([cx,cy,cx+CW*S,cy+CH*S], 5*S, fill=(0x9a,0xad,0xbe))  # 近似玻璃卡
        for (x,y,w,h,r,c) in blocks:
            d.rounded_rectangle([cx+x*S, cy+y*S, cx+(x+w)*S, cy+(y+h)*S],
                                (min(w,h)*S//2) if r==CIRCLE else r*S, fill=c)
        d.text((cx, cy+(CH+2)*S), name, font=f, fill=(0xd0,0xdd,0xe8))
    img.save(path); print("wrote", path, img.size)

render(6, "/tmp/icons_6x.png")
render(1, "/tmp/icons_1x.png")
# 1x 就是上板看到的真实分辨率，放大 8 倍做最近邻，检查糊不糊
im = Image.open("/tmp/icons_1x.png"); im.resize((im.width*8, im.height*8), Image.NEAREST).save("/tmp/icons_1x_zoom.png")
