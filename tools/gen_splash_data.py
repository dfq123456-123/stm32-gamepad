# -*- coding: utf-8 -*-
"""生成 OLED 开机动画所需数据: 头像 28x28 单色位图 + 中文字模(16x16)
输出: D:/stm32/project/手柄/HARDWARE/boot_splash_data.c
取模格式: 行优先, 每行 MSB 在前(左像素=高位), 1=点亮
"""
from PIL import Image, ImageFont, ImageDraw

SRC = r"C:\Users\admin\Pictures\头像\b_a08c9b60ae4f5eb18d65919525a11ad0.jpg"
OUT = r"D:\stm32\project\手柄\HARDWARE\boot_splash_data.c"

FACE = 32                     # 头像边长(像素)
FONTS = [r"C:\Windows\Fonts\simhei.ttf",
         r"C:\Windows\Fonts\msyh.ttc",
         r"C:\Windows\Fonts\simsun.ttc"]

def load_font(size):
    for p in FONTS:
        try:
            return ImageFont.truetype(p, size)
        except OSError:
            continue
    raise RuntimeError("no CJK font found")

def face_bitmap(size=FACE):
    """头像 -> size x size 二值位图, 圆形裁剪, 返回 size*ceil(size/8) 字节"""
    img = Image.open(SRC).convert("L").resize((size, size), Image.LANCZOS)
    px = img.load()
    cx = cy = size / 2 - 0.5
    r2 = (size / 2 + 0.3) * (size / 2 + 0.3)   # 圆形裁剪: 脸占满, 圆外(背景)置黑
    stride = (size + 7) // 8
    data = []
    for y in range(size):
        for bx in range(stride):
            b = 0
            for i in range(8):
                x = bx * 8 + i
                if x >= size:
                    data.append(b)
                    break
                dx, dy = x - cx, y - cy
                on = (dx*dx + dy*dy) <= r2 and px[x, y] > 120
                b = (b << 1) | (1 if on else 0)
            else:
                data.append(b)
    return data

def glyph(ch, size, cell, font):
    """渲染单个字符到 cell x cell, 返回每行 2 字节(MSB)的列表"""
    im = Image.new("L", (cell, cell), 255)
    d = ImageDraw.Draw(im)
    bbox = d.textbbox((0, 0), ch, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((cell - w) // 2 - bbox[0], (cell - h) // 2 - bbox[1]),
           ch, font=font, fill=0)
    out = []
    for y in range(cell):
        row = 0
        for x in range(cell):
            row = (row << 1) | (1 if im.getpixel((x, y)) < 128 else 0)
        out += [(row >> 8) & 0xFF, row & 0xFF]
    return out

def main():
    face = face_bitmap(FACE)
    f16 = load_font(14)

    duzi  = "肚子隆起来了"        # 16x16, 6 字
    dfq   = "达芬奇w"             # 14x14, 3 汉字 + ASCII w(比 12 清晰, 布局需要)

    g16 = []
    for ch in duzi:
        g16 += glyph(ch, 14, 16, f16)
    g14 = []
    for ch in dfq:
        g14 += glyph(ch, 14, 14, f16)

    def cbytes(arr, indent="    "):
        lines = []
        for i in range(0, len(arr), 12):
            lines.append(indent + ", ".join("0x%02X" % v for v in arr[i:i+12]) + ",")
        return "\n".join(lines).rstrip(",")

    src = """/**
  ******************************************************************************
  * @file    boot_splash_data.c
  * @brief   开机动画数据: 头像 28x28 单色位图 + 中文字模(自动生成, 勿手改)
  *          生成工具: tools/gen_splash_data.py (PIL)
  *          取模: 行优先, 每行 MSB 在前(左像素=高位), 1=点亮
  ******************************************************************************
  */
#include "boot_splash.h"

/* 头像位图 32x32: 每行 4 字节(32/8=4), 共 128 字节 */
const uint8_t g_face_bmp[BOOT_FACE_H * ((BOOT_FACE_W + 7) / 8)] =
{
%s
};

/* "肚子隆起来了" 16x16 字模, 6 字 x 32 字节(每行 2 字节) */
const uint8_t g_cn_duzi[BOOT_DUZI_NUM * 32] =
{
%s
};

/* "达芬奇w" 14x14 字模, 4 字符 x 28 字节(每行 2 字节) */
const uint8_t g_cn_dfq[BOOT_DFQ_NUM * 28] =
{
%s
};
""" % (cbytes(face), cbytes(g16), cbytes(g14))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write(src)
    print("written:", OUT)
    print("face bytes:", len(face), " duzi bytes:", len(g16), " dfq bytes:", len(g14))

if __name__ == "__main__":
    main()
