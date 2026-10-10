# -*- coding: utf-8 -*-
"""
把 PNG/JPG 转成 C 数组（RGB565），方便直接贴进工程显示。

用法：
    python my_app/png2rgb565.py <图片>                     # 按原尺寸转
    python my_app/png2rgb565.py <图片> 32 32               # 缩放到 32x32
    python my_app/png2rgb565.py <图片> --key 0xF81F        # 把某个颜色当透明色键

输出：
    <图片名>.h —— 形如
        #define MYIMG_W 32
        #define MYIMG_H 32
        extern const uint16_t MYIMG[MYIMG_W * MYIMG_H];

用法（工程里）：
    #include "myimg.h"
    ST7735_DrawBitmapRGB(x, y, MYIMG_W, MYIMG_H, MYIMG, 0xF81F);  /* 最后是色键 */
"""

import os
import sys

import numpy as np
from PIL import Image


def to565(img):
    a = np.asarray(img.convert("RGB"), dtype=np.uint16)
    return (((a[:, :, 0] & 0xF8) << 8) |
            ((a[:, :, 1] & 0xFC) << 3) |
            ((a[:, :, 2] & 0xF8) >> 3)).astype(np.uint16)


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1

    path = args[0]
    size = None
    if len(args) >= 3:
        size = (int(args[1]), int(args[2]))

    img = Image.open(path)
    if size:
        img = img.resize(size, Image.LANCZOS)
    img = img.convert("RGB")

    v = to565(img)
    name = os.path.splitext(os.path.basename(path))[0]
    name = "".join(ch if ch.isalnum() else "_" for ch in name).upper()
    out = os.path.join(os.path.dirname(os.path.abspath(path)), name + ".h")

    w, h = img.size
    with open(out, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("/* 由 my_app/png2rgb565.py 从 %s 生成 */\n" % os.path.basename(path))
        f.write("#ifndef _%s_H_\n#define _%s_H_\n\n#include \"stm32f10x.h\"\n\n" % (name, name))
        f.write("#define %s_W %d\n#define %s_H %d\n\n" % (name, w, name, h))
        f.write("const uint16_t %s[%s_W * %s_H] = {\n" % (name, name, name))
        flat = v.reshape(-1)
        for i in range(0, flat.size, 12):
            f.write("    " + ",".join("0x%04X" % c for c in flat[i:i + 12]) + ",\n")
        f.write("};\n\n#endif\n")

    print("已生成 %s  (%dx%d, 占 Flash %d 字节)" % (out, w, h, v.size * 2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
