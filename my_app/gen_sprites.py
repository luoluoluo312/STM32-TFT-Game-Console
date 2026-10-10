# -*- coding: utf-8 -*-
"""
生成 user/sprites.h / sprites.c：太空射击用的像素精灵（16 色调色板 + 索引数据）。

用法：
    python my_app/gen_sprites.py

设计要点：
  - 所有精灵共用一张 16 色调色板，调色板 0 号是"透明"（色键），
    运行时索引 0 的像素直接跳过不画，所以精灵天然带透明通道。
  - 飞船/敌机的轮廓（描边）由脚本自动加：透明的格子只要挨着不透明格子，
    就变成描边色，这样像素画看起来更"立"。
  - 爆炸 6 帧是程序化生成的（一圈圈炸开的碎屑），不是手画的。
  - 每像素 1 字节索引，比直接存 RGB565 省一半 Flash。
  - 另外导出 my_app/sprites_preview.png，方便肉眼检查。
"""

import os
import random

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "user", "sprites.h")
OUT_C = os.path.join(ROOT, "user", "sprites.c")
OUT_PNG = os.path.join(ROOT, "my_app", "sprites_preview.png")

# ---------------------------------------------------------------------------
# 调色板：索引 0 = 透明，其余是霓虹深空配色
# ---------------------------------------------------------------------------
PALETTE = [
    0x0000,   # 0 透明（占位，永远不画）
    0x101830, # 1 描边 深蓝黑
    0x1860A0, # 2 机身 中蓝
    0x38C8F0, # 3 机身亮 霓虹青
    0xE8F8FF, # 4 高光 白
    0xFFE060, # 5 驾驶舱 / 核心 黄
    0xFF7020, # 6 引擎 橙
    0xFFD030, # 7 引擎亮 金
    0xA050FF, # 8 紫
    0x40E080, # 9 绿
    0x303040, # 10 暗灰
    0xC0C0D0, # 11 亮灰
    0xE03030, # 12 红
    0x801010, # 13 暗红
    0x90D8FF, # 14 淡蓝
    0x000000, # 15 黑（不透明）
]

CH2IDX = {'.': 0, 'o': 1, 'b': 2, 'c': 3, 'w': 4, 'y': 5, 'r': 6, 'R': 7,
          'p': 8, 'g': 9, 'k': 10, 'L': 11, 'e': 12, 'E': 13, 'l': 14, 'K': 15}

# ---------------------------------------------------------------------------
# 像素画（点阵图）。'.' 表示透明，其余字符查上面的表。
# ---------------------------------------------------------------------------
PLAYER = [
    ".....c.....",
    ".....c.....",
    "....ccc....",
    "....ccc....",
    "...ccccc...",
    "...cyycc...",
    "..ccccccc..",
    "..ccccccc..",
    ".ccccccccc.",
    "wc.ccccc.cw",
    "...r...r...",
]

SCOUT = [
    "..p...p..",
    "..ppppp..",
    ".ppppppp.",
    "pppwwwppp",
    "ppppppppp",
    ".ppppppp.",
    "..ppppp..",
    "...ppp...",
    "....p....",
]

FIGHTER = [
    "...eeeee...",
    "..eeeeeee..",
    ".eeeeeeeee.",
    "eeewwwwwwee",
    "eeeeeeeeeee",
    ".eeeeeeeee.",
    "..eeeeeee..",
    "...eeeee...",
    "....eee....",
    "....eee....",
    ".....e.....",
]

ELITE = [
    "...ggggggg...",
    "..ggggggggg..",
    ".ggggggggggg.",
    "ggggwwwwwgggg",
    "ggggwwwwwgggg",
    "ggggggggggggg",
    ".ggggggggggg.",
    "..ggggggggg..",
    "...ggggggg...",
    "....ggggg....",
    "....ggggg....",
    ".....ggg.....",
    "......g......",
]

BULLET_P = [
    ".w.",
    ".c.",
    ".c.",
    ".c.",
    ".b.",
    ".b.",
    ".b.",
]

BULLET_E = [
    ".y.",
    ".r.",
    ".r.",
    ".e.",
    ".E.",
]

BOOM_SIZE = 17
BOOM_FRAMES = 6


def to565(rgb):
    """24 位颜色 -> RGB565（屏幕用的是 RGB565）"""
    r = (rgb >> 16) & 0xFF
    g = (rgb >> 8) & 0xFF
    b = rgb & 0xFF
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)


def art_to_idx(art):
    """ASCII 点阵 -> 二维索引数组"""
    w = len(art[0])
    for r in art:
        if len(r) != w:
            raise ValueError("每行长度要一样: %r" % (r,))
    return [[CH2IDX[c] for c in row] for row in art]


def add_outline(idx):
    """自动描边：透明格子只要 4 邻域里有不透明格子，就变成描边色(1)"""
    h, w = len(idx), len(idx[0])
    out = [row[:] for row in idx]
    for y in range(h):
        for x in range(w):
            if idx[y][x] != 0:
                continue
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < h and 0 <= nx < w and idx[ny][nx] not in (0, 1):
                    out[y][x] = 1
                    break
    return out


def shift_rows(idx, first_row, dx):
    """把 first_row 以下的行整体左右平移 dx（做出飞船左右倾斜的姿态）"""
    h, w = len(idx), len(idx[0])
    out = [row[:] for row in idx]
    for y in range(first_row, h):
        row = [0] * w
        for x in range(w):
            nx = x + dx
            if 0 <= nx < w:
                row[nx] = idx[y][x]
        out[y] = row
    return out


def boom_frame(f):
    """程序化生成第 f 帧爆炸（0~5）：一圈圈炸开的碎屑"""
    rnd = random.Random(1000 + f)
    c = (BOOM_SIZE - 1) / 2.0
    rmax = 2.5 + f * 1.8
    hole = max(0.0, (f - 2) * 1.6)
    idx = [[0] * BOOM_SIZE for _ in range(BOOM_SIZE)]

    for y in range(BOOM_SIZE):
        for x in range(BOOM_SIZE):
            d = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            if d > rmax or d < hole:
                continue
            t = d / (rmax + 1e-6)          # 0 = 中心，1 = 最外圈
            if f <= 1:
                col = 4 if t < 0.45 else (5 if t < 0.75 else 6)   # 白->黄->橙
            elif f <= 3:
                col = 5 if t < 0.35 else (6 if t < 0.70 else 7)   # 黄->橙->金
            else:
                col = 6 if t < 0.40 else (12 if t < 0.75 else 13) # 橙->红->暗红
            if t > 0.55 and rnd.random() < 0.40:                  # 边缘打碎
                continue
            idx[y][x] = col
    return idx


def emit_array(f, name, idx):
    h, w = len(idx), len(idx[0])
    flat = [v for row in idx for v in row]
    f.write("const uint8_t %s[%d] = {   /* %dx%d */\n" % (name, len(flat), w, h))
    for i in range(0, len(flat), 16):
        f.write("    " + ",".join("%2d" % v for v in flat[i:i + 16]) + ",\n")
    f.write("};\n\n")


def main():
    pm = add_outline(art_to_idx(PLAYER))
    pl = add_outline(shift_rows(art_to_idx(PLAYER), 6, -1))   # 左倾：尾部往左甩
    pr = add_outline(shift_rows(art_to_idx(PLAYER), 6, +1))   # 右倾：尾部往右甩
    scout = add_outline(art_to_idx(SCOUT))
    fighter = add_outline(art_to_idx(FIGHTER))
    elite = add_outline(art_to_idx(ELITE))
    bp = art_to_idx(BULLET_P)
    be = art_to_idx(BULLET_E)
    booms = [boom_frame(i) for i in range(BOOM_FRAMES)]

    with open(OUT_H, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/**\n")
        f.write("  ******************************************************************************\n")
        f.write("  * @file    sprites.h\n")
        f.write("  * @brief   太空射击用的像素精灵，由 my_app/gen_sprites.py 生成\n")
        f.write("  *\n")
        f.write("  * 每像素 1 字节，是 SPRITE_PALETTE 的下标；下标 0 表示透明，不画。\n")
        f.write("  * 比例：飞船 %dx%d，侦察机 %dx%d，战斗机 %dx%d，精英机 %dx%d，爆炸 %dx%d\n" %
                (len(pm[0]), len(pm), len(scout[0]), len(scout), len(fighter[0]), len(fighter),
                 len(elite[0]), len(elite), BOOM_SIZE, BOOM_SIZE))
        f.write("  ******************************************************************************\n")
        f.write("  */\n")
        f.write("#ifndef _SPRITES_H_\n#define _SPRITES_H_\n\n")
        f.write("#include \"stm32f10x.h\"\n\n")
        f.write("#define SPRITE_PALETTE_COUNT 16\n")
        f.write("extern const uint16_t SPRITE_PALETTE[SPRITE_PALETTE_COUNT];\n\n")
        w = len(pm[0]); h = len(pm)
        f.write("#define PLAYER_W %d\n#define PLAYER_H %d\n" % (w, h))
        f.write("extern const uint8_t SPR_PLAYER[3][PLAYER_W * PLAYER_H];\n\n")
        for nm, mm in (("SCOUT", scout), ("FIGHTER", fighter), ("ELITE", elite)):
            f.write("#define SPR_%s_W %d\n#define SPR_%s_H %d\n" % (nm, len(mm[0]), nm, len(mm)))
            f.write("extern const uint8_t SPR_%s[SPR_%s_W * SPR_%s_H];\n\n" % (nm, nm, nm))
        f.write("#define BULLET_P_W %d\n#define BULLET_P_H %d\n" % (len(bp[0]), len(bp)))
        f.write("extern const uint8_t SPR_BULLET_P[BULLET_P_W * BULLET_P_H];\n\n")
        f.write("#define BULLET_E_W %d\n#define BULLET_E_H %d\n" % (len(be[0]), len(be)))
        f.write("extern const uint8_t SPR_BULLET_E[BULLET_E_W * BULLET_E_H];\n\n")
        f.write("#define BOOM_W %d\n#define BOOM_H %d\n#define BOOM_FRAMES %d\n" %
                (BOOM_SIZE, BOOM_SIZE, BOOM_FRAMES))
        f.write("extern const uint8_t SPR_BOOM[BOOM_FRAMES][BOOM_W * BOOM_H];\n\n")
        f.write("#endif\n")

    with open(OUT_C, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/* 本文件由 my_app/gen_sprites.py 自动生成，不要手改 */\n")
        f.write('#include "sprites.h"\n\n')
        f.write("const uint16_t SPRITE_PALETTE[SPRITE_PALETTE_COUNT] = {\n    ")
        f.write(", ".join("0x%04X" % to565(c) for c in PALETTE))
        f.write("\n};\n\n")

        f.write("const uint8_t SPR_PLAYER[3][PLAYER_W * PLAYER_H] = {\n")
        for idx, tag in ((pm, "正常"), (pl, "左倾"), (pr, "右倾")):
            f.write("    {   /* %s */\n" % tag)
            flat = [v for row in idx for v in row]
            for i in range(0, len(flat), 16):
                f.write("    " + ",".join("%2d" % v for v in flat[i:i + 16]) + ",\n")
            f.write("    },\n")
        f.write("};\n\n")

        emit_array(f, "SPR_SCOUT", scout)
        emit_array(f, "SPR_FIGHTER", fighter)
        emit_array(f, "SPR_ELITE", elite)
        emit_array(f, "SPR_BULLET_P", bp)
        emit_array(f, "SPR_BULLET_E", be)

        f.write("const uint8_t SPR_BOOM[BOOM_FRAMES][BOOM_W * BOOM_H] = {\n")
        for i, bm in enumerate(booms):
            f.write("    {   /* 第 %d 帧 */\n" % i)
            flat = [v for row in bm for v in row]
            for k in range(0, len(flat), 16):
                f.write("    " + ",".join("%2d" % v for v in flat[k:k + 16]) + ",\n")
            f.write("    },\n")
        f.write("};\n")

    # 预览图：把所有精灵画到深色底上，放大 4 倍
    pad, scale = 4, 4
    items = [("player", pm)] + \
            [("left", pl), ("right", pr), ("scout", scout), ("fighter", fighter),
             ("elite", elite), ("bulletP", bp), ("bulletE", be)] + \
            [("boom%d" % i, b) for i, b in enumerate(booms)]
    cellw = max(len(i[0]) for _, i in items) + pad * 2
    cellh = max(len(i) for _, i in items) + pad * 2
    cols = 6
    rows = (len(items) + cols - 1) // cols
    img = Image.new("RGB", (cellw * cols, cellh * rows), (6, 8, 24))
    for n, (_, idx) in enumerate(items):
        ox = (n % cols) * cellw + pad
        oy = (n // cols) * cellh + pad
        for y, row in enumerate(idx):
            for x, v in enumerate(row):
                if v:
                    c = to565(PALETTE[v])
                    img.putpixel((ox + x, oy + y),
                                 ((c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3))
    img.resize((img.width * scale, img.height * scale), 0).save(OUT_PNG)

    print("精灵已生成:", OUT_C)
    print("预览图    :", OUT_PNG)


if __name__ == "__main__":
    main()
