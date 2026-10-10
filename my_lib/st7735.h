/**
  ******************************************************************************
  * @file    st7735.h
  * @brief   ST7735S 1.8 寸 TFT 彩屏驱动（SPI，128x160，RGB565）
  *
  * 这是一个可复用的显示模块驱动，接口和 my_lib/oled.c 是一个风格：
  *   初始化 -> 开窗口 -> 填色 / 画实心圆。
  * 只用到 SPI1 的两个引脚（SCK/MOSI），另外用 3 个普通 GPIO 控制
  * CS / DC / RST，背光 BLK 直接接 3V3 常亮。
  ******************************************************************************
  */
#ifndef _ST7735_H_
#define _ST7735_H_

#include "stm32f10x.h"

/* ==========================================================================
 *  一、硬件引脚配置（改接线只改这里）
 *  1.8 寸 ST7735 模块丝印一般是：GND VCC SCL SDA RES DC CS BLK
 * ========================================================================== */
#define ST7735_SPI           SPI1
#define ST7735_GPIO          GPIOA
#define ST7735_SCK_PIN       GPIO_Pin_5    /* PA5 -> SCL，SPI1_SCK  */
#define ST7735_MOSI_PIN      GPIO_Pin_7    /* PA7 -> SDA，SPI1_MOSI */
#define ST7735_CS_PIN        GPIO_Pin_4    /* PA4 -> CS （低有效）   */
#define ST7735_DC_PIN        GPIO_Pin_1    /* PA1 -> DC （0=命令 1=数据） */
#define ST7735_RST_PIN       GPIO_Pin_0    /* PA0 -> RES（低有效）   */

/* SPI1 挂在 APB2(72MHz) 上，分频决定屏幕刷新速度：
 *   div4 -> 18MHz（默认，通用性好）
 *   div2 -> 36MHz（更快，个别模块会花屏）
 *   div8 -> 9MHz （最稳，刷新慢）
 * 花屏/雪花了就把这个宏改大一级 */
#define ST7735_SPI_PRESCALER SPI_BaudRatePrescaler_4

/* ==========================================================================
 *  二、屏幕尺寸与方向
 * ========================================================================== */
#define ST7735_WIDTH         128           /* 竖屏宽 = 面板原生宽 */
#define ST7735_HEIGHT        160           /* 竖屏高 = 面板原生高 */

/* 显示方向：用 ST7735_SetRotation() 在运行时切换，宽高会自动对调 */
#define ST7735_ROT_PORTRAIT        0       /* 竖屏 128x160（默认） */
#define ST7735_ROT_LANDSCAPE       1       /* 横屏 160x128 */
#define ST7735_ROT_PORTRAIT_FLIP   2       /* 竖屏翻转 */
#define ST7735_ROT_LANDSCAPE_FLIP  3       /* 横屏翻转 */

/* 各方向对应的 MADCTL(0x36) 值。
 * bit3 是 BGR 位：置 1 = 把数据当 BGR，置 0 = 当 RGB。
 * 你手上这块屏实测要 **置 0**（所以是 0xC0/0xA0/0x00/0x60）；
 * 如果换成带 BGR 的值（如 0xC8），会出现"蓝天变黄、黄色变蓝"的红蓝互换。 */
#define ST7735_MADCTL_PORTRAIT        0xC0
#define ST7735_MADCTL_LANDSCAPE       0xA0
#define ST7735_MADCTL_PORTRAIT_FLIP   0x00
#define ST7735_MADCTL_LANDSCAPE_FLIP  0x60

/* GRAM 原点偏移：个别模块可视区偏离显存原点 1~2 像素，表现是画面整体
 * 偏移或边上有一条杂色/雪花。遇到这种情况把这两个值改成 1 或 2 试。
 * 绝大多数模块保持 0 即可。 */
#define ST7735_X_OFFSET      0
#define ST7735_Y_OFFSET      0

/* ==========================================================================
 *  三、颜色（RGB565，一个像素 2 字节）
 *  用 ST7735_RGB565(r,g,b) 把 R/G/B 各 8 位压成 16 位
 * ========================================================================== */
#define ST7735_RGB565(r, g, b)  ((uint16_t)((((r) & 0xF8) << 8) | \
                                             (((g) & 0xFC) << 3) | \
                                             (((b) & 0xF8) >> 3)))

#define ST7735_BLACK      0x0000
#define ST7735_WHITE      0xFFFF
#define ST7735_RED        0xF800
#define ST7735_GREEN      0x07E0
#define ST7735_BLUE       0x001F
#define ST7735_YELLOW     0xFFE0
#define ST7735_CYAN       0x07FF
#define ST7735_MAGENTA    0xF81F

/* ==========================================================================
 *  四、接口函数
 * ========================================================================== */

// @简介：初始化 SPI1、GPIO 和 ST7735 寄存器，最后把屏幕清成黑色
// @返回值：无
void ST7735_Init(void);

// @简介：设置显示方向（设置完屏幕宽高会自动对调，内容会清掉）
// @参数：Rotation - ST7735_ROT_PORTRAIT / LANDSCAPE / PORTRAIT_FLIP / LANDSCAPE_FLIP
void ST7735_SetRotation(uint8_t Rotation);

// @简介：取当前方向下的屏幕宽 / 高（像素）
uint16_t ST7735_GetWidth(void);
uint16_t ST7735_GetHeight(void);

// @简介：设置"背景色"。画实心圆时圆外就填这个颜色（用来擦掉旧位置）
// @参数：Color - RGB565 颜色
void ST7735_SetBackColor(uint16_t Color);

// @简介：开一个矩形窗口，之后写入的像素会依次填满这个窗口
// @参数：X, Y   - 窗口左上角坐标
// @参数：Width  - 窗口宽（像素）
// @参数：Height - 窗口高（像素）
void ST7735_SetWindow(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height);

// @简介：把整屏填成一种颜色
// @参数：Color - RGB565 颜色
void ST7735_Fill(uint16_t Color);

// @简介：把一个矩形区域填成一种颜色
// @参数：X, Y   - 左上角坐标；Width, Height - 尺寸；Color - 颜色
void ST7735_FillRect(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, uint16_t Color);

// @简介：画一个像素
// @参数：X, Y - 坐标；Color - 颜色
void ST7735_DrawPixel(uint16_t X, uint16_t Y, uint16_t Color);

// @简介：画实心圆（圆外自动填背景色，所以它同时也把旧位置擦掉了）
// @参数：CX, CY - 圆心坐标；Radius - 半径；Color - RGB565 颜色
void ST7735_DrawFilledCircle(int16_t CX, int16_t CY, int16_t Radius, uint16_t Color);

// @简介：显示一张图片（RGB565 像素数组，按行优先，行内从左到右）
// @参数：X, Y   - 图片左上角坐标
// @参数：Width, Height - 图片宽高（像素）
// @参数：pPixels - 像素数组，第 (行*Width + 列) 个元素
void ST7735_DrawImage(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height,
                      const uint16_t *pPixels);

/* ==========================================================================
 *  四、DMA + 逐行合成（游戏局部刷新用）
 *
 *  这块屏没有显存（RAM 太小，放不下整屏缓冲），所以"改哪里画哪里"：
 *  把要更新的矩形一行一行地在行缓冲里拼好，再用 DMA 把这一行推给屏幕。
 *  用法：GFX_LineStart(x0,x1,y) -> 若干 GFX_LineXXX 往这一行上叠东西 -> GFX_LineFlush()
 * ========================================================================== */

#define ST7735_LINE_MAX      160        /* 行缓冲能装的最大像素数（屏幕最长边） */

// @简介：初始化 SPI1 发送用的 DMA（DMA1 通道3）；ST7735_Init 里会自动调用
void ST7735_DMA_Init(void);

// @简介：开始合成一行（x0~x1 是这一行要画的范围，y 是行号）
void GFX_LineStart(int16_t X0, int16_t X1, int16_t Y);

// @简介：把当前这一行填成一种颜色（通常用来铺背景）
void GFX_LineFill(uint16_t Color);

// @简介：在当前行上点一个像素（超出这一行的范围会自动忽略）
void GFX_LinePixel(int16_t X, uint16_t Color);

// @简介：把一个索引精灵的"某一行"叠到当前行上（叠第几行由精灵的 Y 和当前行号决定）
// @参数：X,Y - 精灵左上角；W,H - 尺寸；pIdx - 索引数据；pPal - 调色板
// @参数：Tint - 0 = 按调色板画；非 0 = 整个精灵都用这个颜色画（受击闪白用）
void GFX_LineSpriteRow(int16_t X, int16_t Y, uint8_t W, uint8_t H,
                       const uint8_t *pIdx, const uint16_t *pPal, uint16_t Tint);

// @简介：把一串 8x8 文字的"某一行"叠到当前行上
void GFX_LineTextRow(int16_t X, int16_t Y, const char *Str, uint16_t Color);

// @简介：把当前这一行用 DMA 推给屏幕（用前必须先 GFX_LineStart）
void GFX_LineFlush(void);

/* ==========================================================================
 *  五、通用图形（直接叠加到屏幕现有画面上，逐点写）
 * ========================================================================== */

// @简介：显示 ASCII 字符串（8x8 点阵，只画笔画，不画背景）
void ST7735_DrawText(int16_t X, int16_t Y, const char *Str, uint16_t Color);

// @简介：显示一张 RGB565 位图，Key 色的像素不画（色键透明）
void ST7735_DrawBitmapRGB(int16_t X, int16_t Y, uint16_t W, uint16_t H,
                          const uint16_t *pPixels, uint16_t Key);

// @简介：画直线
void ST7735_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, uint16_t Color);

// @简介：画圆（空心）
void ST7735_DrawCircle(int16_t CX, int16_t CY, int16_t Radius, uint16_t Color);

// @简介：把一段"已经按 高字节/低字节 排好的"像素数据直接推给屏幕
//        照片存在外部 Flash(W25Q64) 里时，读出来就是这种格式，直接推最快
// @参数：X,Y - 左上角；W,H - 尺寸；pBytes - 数据，共 W*H*2 字节
void ST7735_PushBytes(int16_t X, int16_t Y, uint16_t W, uint16_t H, const uint8_t *pBytes);

#endif
