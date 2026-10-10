# -*- coding: utf-8 -*-
"""
生成 my_lib/font8x8.h / font8x8.c：8x8 点阵 ASCII 字库（字符 32~126，共 95 个）。

用法：
    python my_app/gen_font8x8.py

每个字符 8 字节，每字节是一行，最高位在左边（和 ST7735 的显示习惯一致）。
HUD 上的分数、最高分、提示文字都用它。
"""

import os

from PIL import Image, ImageDraw, ImageFont

FIRST, LAST = 32, 126
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "my_lib", "font8x8.h")
OUT_C = os.path.join(ROOT, "my_lib", "font8x8.c")

FONTS = [
    r"C:\Windows\Fonts\consola.ttf",
    r"C:\Windows\Fonts\cour.ttf",
    r"C:\Windows\Fonts\lucon.ttf",
]


def load_font(size):
    for p in FONTS:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default(size=size)


def glyph(ch, font):
    """把一个字符渲染成 8x8 点阵，返回 8 个字节（每字节一行，bit7 在最左）。"""
    img = Image.new("L", (24, 24), 0)
    ImageDraw.Draw(img).text((6, 4), ch, fill=255, font=font)

    bbox = img.getbbox()
    if bbox is None:                 # 空格之类没有墨迹
        return [0] * 8

    ink = img.crop(bbox)

    # 太高/太宽就等比缩一下，保证塞进 8x8（上面留 1 行，左右居中）
    maxw, maxh = 8, 7
    if ink.width > maxw or ink.height > maxh:
        s = min(maxw / ink.width, maxh / ink.height)
        ink = ink.resize((max(1, int(ink.width * s)), max(1, int(ink.height * s))),
                         Image.LANCZOS)

    cell = Image.new("L", (8, 8), 0)
    cell.paste(ink, ((8 - ink.width) // 2, 1 + (maxh - ink.height) // 2))

    px = cell.load()
    rows = []
    for y in range(8):
        b = 0
        for x in range(8):
            if px[x, y] >= 96:
                b |= (0x80 >> x)
        rows.append(b)
    return rows


def main():
    font = load_font(11)
    data = [glyph(chr(c), font) for c in range(FIRST, LAST + 1)]

    with open(OUT_H, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/**\n")
        f.write("  ******************************************************************************\n")
        f.write("  * @file    font8x8.h\n")
        f.write("  * @brief   8x8 ASCII 点阵字库（字符 32~126），由 my_app/gen_font8x8.py 生成\n")
        f.write("  *\n")
        f.write("  * 每个字符 8 个字节，每字节是一行，bit7 在最左边。\n")
        f.write("  * 取字：FONT8X8[字符 - 32][行]\n")
        f.write("  ******************************************************************************\n")
        f.write("  */\n")
        f.write("#ifndef _FONT8X8_H_\n#define _FONT8X8_H_\n\n")
        f.write("#include \"stm32f10x.h\"\n\n")
        f.write("#define FONT8X8_FIRST   32\n")
        f.write("#define FONT8X8_COUNT   %d\n" % (LAST - FIRST + 1))
        f.write("#define FONT8X8_W       8\n")
        f.write("#define FONT8X8_H       8\n\n")
        f.write("/* 字符 c(32~126) 的第 r(0~7) 行点阵 */\n")
        f.write("extern const uint8_t FONT8X8[FONT8X8_COUNT][FONT8X8_H];\n\n")
        f.write("#endif\n")

    with open(OUT_C, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/* 本文件由 my_app/gen_font8x8.py 自动生成，不要手改 */\n")
        f.write('#include "font8x8.h"\n\n')
        f.write("const uint8_t FONT8X8[FONT8X8_COUNT][FONT8X8_H] = {\n")
        for i, rows in enumerate(data):
            ch = chr(FIRST + i)
            name = ch if ch != "\\" else "\\\\"
            f.write("    { %s },  /* '%s' */\n" %
                    (", ".join("0x%02X" % b for b in rows), name))
        f.write("};\n")

    print("字库已生成:", OUT_C)


if __name__ == "__main__":
    main()
