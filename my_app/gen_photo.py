# -*- coding: utf-8 -*-
"""
把一张图片转成"照片页"用的 C 数组，生成 user/photo.c 与 user/photo.h。

用法：
    python my_app/gen_photo.py <图片路径>
    python my_app/gen_photo.py --placeholder      # 生成一张占位图

为什么要转成 256 色：
    直接存 RGB565 要 128*160*2 = 40KB，单片机内部 Flash 一共才 64KB；
    转成"256 色调色板 + 每像素 1 字节"只要 128*160 + 512 = 20.5KB，省一半，
    在 1.8 寸屏上几乎看不出区别。

存进 Flash 的东西：
    PHOTO_PAL[256]      调色板（RGB565）
    PHOTO_IDX[128*160]  每个像素一个下标，指到 PHOTO_PAL
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 128, 160
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_C = os.path.join(ROOT, "user", "photo.c")
OUT_H = os.path.join(ROOT, "user", "photo.h")


def cover(img, w, h):
    """等比缩放到刚好盖住 w*h，再居中裁剪（不变形）"""
    sw, sh = img.size
    s = max(w / sw, h / sh)
    img = img.resize((max(1, int(round(sw * s))), max(1, int(round(sh * s)))),
                     Image.LANCZOS)
    left = (img.width - w) // 2
    top = (img.height - h) // 2
    return img.crop((left, top, left + w, top + h))


def placeholder():
    """还没提供照片时用的占位图"""
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W - 1, y)],
               fill=(int(16 + 90 * t), int(24 + 40 * t), int(80 + 130 * t)))
    d.rectangle([5, 5, W - 6, H - 6], outline=(255, 255, 255))

    def font(size):
        try:
            return ImageFont.load_default(size=size)
        except Exception:
            return None

    d.text((16, 62), "PHOTO", fill=(255, 255, 255), font=font(28))
    d.text((20, 96), "128 x 160", fill=(190, 215, 255), font=font(16))
    return img


def to565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)


def write_files(img):
    # 转成 256 色的调色板图（PIL 的 median-cut 自适应调色板）
    q = img.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT)
    pal = q.getpalette()
    idx = q.tobytes()                       # 每像素 1 字节，行优先
    assert len(idx) == W * H, len(idx)

    pal565 = [to565(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) for i in range(256)]

    with open(OUT_H, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/**\n")
        f.write("  ******************************************************************************\n")
        f.write("  * @file    photo.h\n")
        f.write("  * @brief   照片页的数据（%dx%d，256 色调色板），由 gen_photo.py 生成\n" % (W, H))
        f.write("  *\n")
        f.write("  * 调色板 + 下标总占用 = %d + %d = %d 字节(约 %.1fKB)\n" %
                (256 * 2, W * H, 256 * 2 + W * H, (256 * 2 + W * H) / 1024.0))
        f.write("  ******************************************************************************\n")
        f.write("  */\n")
        f.write("#ifndef _PHOTO_H_\n#define _PHOTO_H_\n\n")
        f.write("#include \"stm32f10x.h\"\n\n")
        f.write("#define PHOTO_WIDTH   %d\n#define PHOTO_HEIGHT  %d\n" % (W, H))
        f.write("#define PHOTO_COLORS  256\n\n")
        f.write("extern const uint16_t PHOTO_PAL[PHOTO_COLORS];\n")
        f.write("extern const uint8_t  PHOTO_IDX[PHOTO_WIDTH * PHOTO_HEIGHT];\n\n")
        f.write("#endif\n")

    with open(OUT_C, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/* 本文件由 my_app/gen_photo.py 自动生成，不要手改 */\n")
        f.write('#include "photo.h"\n\n')

        f.write("const uint16_t PHOTO_PAL[PHOTO_COLORS] = {\n")
        for i in range(0, 256, 8):
            f.write("    " + ",".join("0x%04X" % c for c in pal565[i:i + 8]) + ",\n")
        f.write("};\n\n")

        f.write("const uint8_t PHOTO_IDX[PHOTO_WIDTH * PHOTO_HEIGHT] = {\n")
        for i in range(0, len(idx), 24):
            f.write("    " + ",".join("%3d" % v for v in idx[i:i + 24]) + ",\n")
        f.write("};\n")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    if args[0] == "--placeholder":
        img, src = placeholder(), "(占位图)"
    else:
        img, src = Image.open(args[0]), args[0]

    img = cover(img, W, H)
    write_files(img)

    print("源图片 : %s" % src)
    print("输出   : %s  +  %s" % (OUT_C, OUT_H))
    print("尺寸   : %dx%d, 占 Flash %d 字节 (约 %.1fKB)" % (W, H, 256 * 2 + W * H,
                                                          (256 * 2 + W * H) / 1024.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
