/**
  ******************************************************************************
  * @file    st7735.c
  * @brief   ST7735S 1.8 寸 TFT 彩屏驱动源文件（SPI，128x160，RGB565）
  *
  *  硬件 SPI1 + 3 根控制线（CS/DC/RST）。
  *  和 ST7735 通信就两种"包"：
  *    - 命令：DC=0，把命令字节发出去
  *    - 数据：DC=1，把数据字节发出去
  *  屏幕内部有一块显存(GRAM)，要画东西的三步固定是：
  *    1) 发 0x2A / 0x2B 设好"列范围 / 行范围"（即开一个矩形窗口）
  *    2) 发 0x2C，表示"下面开始是像素数据"
  *    3) 把窗口内每个像素的 16 位颜色按顺序发过去
  *  坐标会自动折行填满窗口，不用自己算行。
  ******************************************************************************
  */

#include "st7735.h"
#include "delay.h"
#include "font8x8.h"
#include <math.h>

/* 引脚电平操作宏：比调用 GPIO_SetBits 少一层函数调用，画图时快一点 */
#define CS_LOW()     GPIO_ResetBits(ST7735_GPIO, ST7735_CS_PIN)
#define CS_HIGH()    GPIO_SetBits(ST7735_GPIO, ST7735_CS_PIN)
#define DC_CMD()     GPIO_ResetBits(ST7735_GPIO, ST7735_DC_PIN)   /* DC=0：命令 */
#define DC_DATA()    GPIO_SetBits(ST7735_GPIO, ST7735_DC_PIN)     /* DC=1：数据 */
#define RST_LOW()    GPIO_ResetBits(ST7735_GPIO, ST7735_RST_PIN)
#define RST_HIGH()   GPIO_SetBits(ST7735_GPIO, ST7735_RST_PIN)

/* 存储"背景色"，画圆时圆外用它填 */
static uint16_t s_bgColor = 0x0000;

/* 当前方向下的屏幕宽高（由 ST7735_SetRotation 维护，竖屏 128x160 / 横屏 160x128） */
static uint16_t s_width  = ST7735_WIDTH;
static uint16_t s_height = ST7735_HEIGHT;

/* 4 个方向对应的 MADCTL 值（bit3 BGR 固定为 0，见 st7735.h 的说明） */
static const uint8_t s_madctl[4] =
{
    ST7735_MADCTL_PORTRAIT,        /* 0 竖屏 128x160 */
    ST7735_MADCTL_LANDSCAPE,       /* 1 横屏 160x128 */
    ST7735_MADCTL_PORTRAIT_FLIP,   /* 2 竖屏翻转 */
    ST7735_MADCTL_LANDSCAPE_FLIP   /* 3 横屏翻转 */
};

/* ST7735 初始化时用的 gamma 表（照抄官方推荐值即可） */
static const uint8_t GAMMA_P[16] =
{
    0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
    0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10
};
static const uint8_t GAMMA_N[16] =
{
    0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
    0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10
};

//
// @简介：发一个字节到 SPI（只等发送缓冲空，不等发完）
// @参数：Data - 要发的字节
//
static void SPI_WriteByte(uint8_t Data)
{
    while (SPI_I2S_GetFlagStatus(ST7735_SPI, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(ST7735_SPI, Data);
}

//
// @简介：等 SPI 把最后一个字节彻底移出去（BSY=0）
//        拉高 CS 之前必须等，否则最后一个字节会被截断
//
static void SPI_WaitIdle(void)
{
    while (SPI_I2S_GetFlagStatus(ST7735_SPI, SPI_I2S_FLAG_BSY) == SET);
}

//
// @简介：发一串字节并等待发送结束
// @参数：pData - 数据首地址；Size - 字节数
//
static void SPI_WriteBytes(const uint8_t *pData, uint32_t Size)
{
    while (Size--) SPI_WriteByte(*pData++);
    SPI_WaitIdle();
}

//
// @简介：发一条命令（DC=0）
// @参数：Cmd - 命令字节
//
static void ST7735_WriteCmd(uint8_t Cmd)
{
    DC_CMD();
    CS_LOW();
    SPI_WriteByte(Cmd);
    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：发一个数据字节（DC=1）
// @参数：Data - 数据字节
//
static void ST7735_WriteData8(uint8_t Data)
{
    DC_DATA();
    CS_LOW();
    SPI_WriteByte(Data);
    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：发一串数据字节（DC=1）
// @参数：pData - 数据首地址；Size - 字节数
//
static void ST7735_WriteData(const uint8_t *pData, uint32_t Size)
{
    DC_DATA();
    CS_LOW();
    SPI_WriteBytes(pData, Size);
    CS_HIGH();
}

//
// @简介：设置显示方向。设置完屏幕宽高会对调（竖屏 128x160 / 横屏 160x128）
// @参数：Rotation - ST7735_ROT_PORTRAIT / LANDSCAPE / PORTRAIT_FLIP / LANDSCAPE_FLIP
//
void ST7735_SetRotation(uint8_t Rotation)
{
    Rotation &= 0x03;

    ST7735_WriteCmd(0x36);                  /* MADCTL：方向 + 颜色顺序 */
    ST7735_WriteData8(s_madctl[Rotation]);

    /* 横屏的两个方向：宽高对调 */
    if (Rotation == ST7735_ROT_LANDSCAPE || Rotation == ST7735_ROT_LANDSCAPE_FLIP)
    {
        s_width  = ST7735_HEIGHT;           /* 160 */
        s_height = ST7735_WIDTH;            /* 128 */
    }
    else
    {
        s_width  = ST7735_WIDTH;            /* 128 */
        s_height = ST7735_HEIGHT;           /* 160 */
    }
}

//
// @简介：取当前方向下的屏幕宽 / 高（像素）
//
uint16_t ST7735_GetWidth(void)  { return s_width; }
uint16_t ST7735_GetHeight(void) { return s_height; }

//
// @简介：连续重复发同一个颜色 Count 次（把一块区域刷成同色时最快）
// @参数：Color - RGB565 颜色；Count - 像素个数
//
static void ST7735_RepeatPixel(uint16_t Color, uint32_t Count)
{
    uint8_t hi = (uint8_t)(Color >> 8);
    uint8_t lo = (uint8_t)Color;

    while (Count--)
    {
        SPI_WriteByte(hi);
        SPI_WriteByte(lo);
    }
}

/* ==========================================================================
 *  DMA + 逐行合成
 * ========================================================================== */

/* 行缓冲：每个像素 2 字节，按 高字节/低字节 排好，DMA 直接照搬给 SPI */
static uint8_t s_line[ST7735_LINE_MAX * 2];

static int16_t s_lx0 = 0;          /* 当前这一行要画的 x 范围 */
static int16_t s_lx1 = 0;
static int16_t s_ly  = 0;          /* 当前这一行的 y */

//
// @简介：初始化 SPI1 发送用的 DMA（DMA1 通道3 = SPI1_TX）
//
void ST7735_DMA_Init(void)
{
    DMA_InitTypeDef dma;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA1_Channel3);
    dma.DMA_PeripheralBaseAddr = (uint32_t)(&(ST7735_SPI->DR));
    dma.DMA_MemoryBaseAddr     = (uint32_t)s_line;
    dma.DMA_DIR                = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize         = 1;
    dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    dma.DMA_Mode               = DMA_Mode_Normal;
    dma.DMA_Priority           = DMA_Priority_High;
    dma.DMA_M2M                = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &dma);

    /* 让 SPI1 的发送请求接到 DMA 上 */
    SPI_I2S_DMACmd(ST7735_SPI, SPI_I2S_DMAReq_Tx, ENABLE);
}

//
// @简介：用 DMA 把一串字节推给 SPI，等这次传输彻底结束才返回
// @参数：pData - 数据；Size - 字节数
//
static void SPI_DMA_SendBytes(const uint8_t *pData, uint16_t Size)
{
    if (Size == 0) return;

    DMA_Cmd(DMA1_Channel3, DISABLE);
    DMA1_Channel3->CMAR = (uint32_t)pData;
    DMA_SetCurrDataCounter(DMA1_Channel3, Size);
    DMA_Cmd(DMA1_Channel3, ENABLE);

    while (DMA_GetFlagStatus(DMA1_FLAG_TC3) == RESET);                  /* 等 DMA 搬完 */
    DMA_ClearFlag(DMA1_FLAG_TC3);
    while (SPI_I2S_GetFlagStatus(ST7735_SPI, SPI_I2S_FLAG_BSY) == SET);  /* 等最后一个字节移出去 */

    DMA_Cmd(DMA1_Channel3, DISABLE);
}

//
// @简介：开始合成一行（后面的 GFX_LineXXX 都是往这一行上叠东西）
//
void GFX_LineStart(int16_t X0, int16_t X1, int16_t Y)
{
    if (X0 < 0) X0 = 0;
    if (X1 > (int16_t)s_width - 1) X1 = (int16_t)s_width - 1;
    if (X0 > X1) X0 = X1;

    s_lx0 = X0;
    s_lx1 = X1;
    s_ly  = Y;
}

//
// @简介：把当前这一行填成一种颜色
//
void GFX_LineFill(uint16_t Color)
{
    int16_t i, n = (int16_t)(s_lx1 - s_lx0 + 1);
    uint8_t hi = (uint8_t)(Color >> 8);
    uint8_t lo = (uint8_t)Color;

    for (i = 0; i < n; i++)
    {
        s_line[i * 2]     = hi;
        s_line[i * 2 + 1] = lo;
    }
}

//
// @简介：在当前行上点一个像素（超出范围自动忽略）
//
void GFX_LinePixel(int16_t X, uint16_t Color)
{
    int16_t i;

    if (X < s_lx0 || X > s_lx1) return;

    i = (int16_t)(X - s_lx0);
    s_line[i * 2]     = (uint8_t)(Color >> 8);
    s_line[i * 2 + 1] = (uint8_t)Color;
}

//
// @简介：把索引精灵的某一行叠到当前行上（只画非透明像素）
//
void GFX_LineSpriteRow(int16_t X, int16_t Y, uint8_t W, uint8_t H,
                       const uint8_t *pIdx, const uint16_t *pPal, uint16_t Tint)
{
    const uint8_t *p;
    uint8_t        row;
    int16_t        x, dx;

    if (s_ly < Y || s_ly >= (int16_t)(Y + H)) return;   /* 当前行没落在精灵上 */

    row = (uint8_t)(s_ly - Y);
    p   = pIdx + (uint16_t)row * W;

    for (x = 0; x < (int16_t)W; x++)
    {
        uint8_t v = p[x];
        if (v == 0) continue;                           /* 0 号是透明色键 */

        dx = (int16_t)(X + x);
        if (dx < s_lx0 || dx > s_lx1) continue;

        GFX_LinePixel(dx, Tint ? Tint : pPal[v]);
    }
}

//
// @简介：把 8x8 文字的某一行叠到当前行上
//
void GFX_LineTextRow(int16_t X, int16_t Y, const char *Str, uint16_t Color)
{
    uint8_t row, bits, m;
    int16_t x = X;

    if (s_ly < Y || s_ly >= (int16_t)(Y + FONT8X8_H)) return;

    row = (uint8_t)(s_ly - Y);

    while (*Str)
    {
        uint8_t ch = (uint8_t)(*Str++);

        if (ch < FONT8X8_FIRST || ch >= (FONT8X8_FIRST + FONT8X8_COUNT))
        {
            x += FONT8X8_W;
            continue;
        }

        bits = FONT8X8[ch - FONT8X8_FIRST][row];
        for (m = 0x80; m; m >>= 1)
        {
            if (bits & m) GFX_LinePixel(x, Color);
            x++;
        }
    }
}

//
// @简介：把当前这一行用 DMA 推给屏幕
//
void GFX_LineFlush(void)
{
    uint16_t n = (uint16_t)(s_lx1 - s_lx0 + 1);

    ST7735_SetWindow((uint16_t)s_lx0, (uint16_t)s_ly, n, 1);

    DC_DATA();
    CS_LOW();
    SPI_DMA_SendBytes(s_line, (uint16_t)(n * 2));
    CS_HIGH();
}

/* ==========================================================================
 *  通用图形：直接叠加到屏幕现有画面上（逐点写）
 * ========================================================================== */

//
// @简介：显示 ASCII 字符串（8x8 点阵，只画笔画，背景保持不变）
//
void ST7735_DrawText(int16_t X, int16_t Y, const char *Str, uint16_t Color)
{
    uint8_t row, bits, m, i;
    int16_t x = X;

    while (*Str)
    {
        uint8_t ch = (uint8_t)(*Str++);

        if (ch < FONT8X8_FIRST || ch >= (FONT8X8_FIRST + FONT8X8_COUNT))
        {
            x += FONT8X8_W;
            continue;
        }

        for (row = 0; row < FONT8X8_H; row++)
        {
            bits = FONT8X8[ch - FONT8X8_FIRST][row];
            if (!bits) continue;

            i = 0;
            for (m = 0x80; m; m >>= 1, i++)
            {
                if (bits & m)
                {
                    ST7735_DrawPixel((uint16_t)(x + i), (uint16_t)(Y + row), Color);
                }
            }
        }

        x += FONT8X8_W;
    }
}

//
// @简介：显示 RGB565 位图，Key 色的像素不画（色键透明）
//
void ST7735_DrawBitmapRGB(int16_t X, int16_t Y, uint16_t W, uint16_t H,
                          const uint16_t *pPixels, uint16_t Key)
{
    uint16_t i, j;

    for (j = 0; j < H; j++)
    {
        for (i = 0; i < W; i++)
        {
            uint16_t c = pPixels[(uint32_t)j * W + i];
            if (c == Key) continue;

            ST7735_DrawPixel((uint16_t)(X + i), (uint16_t)(Y + j), c);
        }
    }
}

//
// @简介：画直线（Bresenham）
//
void ST7735_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, uint16_t Color)
{
    int16_t dx, dy, sx, sy, err, e2;

    dx = (int16_t)(X1 - X0); if (dx < 0) dx = (int16_t)(-dx);
    dy = (int16_t)(Y1 - Y0); if (dy < 0) dy = (int16_t)(-dy);
    sx = (X0 < X1) ? 1 : -1;
    sy = (Y0 < Y1) ? 1 : -1;
    err = (int16_t)(dx - dy);

    for (;;)
    {
        ST7735_DrawPixel((uint16_t)X0, (uint16_t)Y0, Color);
        if (X0 == X1 && Y0 == Y1) break;

        e2 = (int16_t)(err * 2);
        if (e2 > -dy) { err -= dy; X0 = (int16_t)(X0 + sx); }
        if (e2 <  dx) { err += dx; Y0 = (int16_t)(Y0 + sy); }
    }
}

//
// @简介：画圆（空心）
//
void ST7735_DrawCircle(int16_t CX, int16_t CY, int16_t Radius, uint16_t Color)
{
    int16_t x = 0, y = Radius, d;

    if (Radius <= 0) return;
    d = (int16_t)(1 - Radius);

    while (x <= y)
    {
        ST7735_DrawPixel((uint16_t)(CX + x), (uint16_t)(CY + y), Color);
        ST7735_DrawPixel((uint16_t)(CX - x), (uint16_t)(CY + y), Color);
        ST7735_DrawPixel((uint16_t)(CX + x), (uint16_t)(CY - y), Color);
        ST7735_DrawPixel((uint16_t)(CX - x), (uint16_t)(CY - y), Color);
        ST7735_DrawPixel((uint16_t)(CX + y), (uint16_t)(CY + x), Color);
        ST7735_DrawPixel((uint16_t)(CX - y), (uint16_t)(CY + x), Color);
        ST7735_DrawPixel((uint16_t)(CX + y), (uint16_t)(CY - x), Color);
        ST7735_DrawPixel((uint16_t)(CX - y), (uint16_t)(CY - x), Color);

        if (d < 0)
        {
            d = (int16_t)(d + 2 * x + 3);
        }
        else
        {
            d = (int16_t)(d + 2 * (x - y) + 5);
            y--;
        }
        x++;
    }
}

//
// @简介：开窗口。之后写入的像素会从窗口左上角开始，一行一行填满窗口
// @参数：X, Y   - 窗口左上角坐标（屏幕坐标，不含 GRAM 偏移）
// @参数：Width  - 窗口宽；Height - 窗口高
//
void ST7735_SetWindow(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height)
{
    uint8_t  buf[4];
    uint16_t X1 = (uint16_t)(X + ST7735_X_OFFSET);
    uint16_t Y1 = (uint16_t)(Y + ST7735_Y_OFFSET);
    uint16_t X2 = (uint16_t)(X + Width  - 1 + ST7735_X_OFFSET);
    uint16_t Y2 = (uint16_t)(Y + Height - 1 + ST7735_Y_OFFSET);

    /* 0x2A：列地址范围 */
    ST7735_WriteCmd(0x2A);
    buf[0] = (uint8_t)(X1 >> 8);
    buf[1] = (uint8_t)X1;
    buf[2] = (uint8_t)(X2 >> 8);
    buf[3] = (uint8_t)X2;
    ST7735_WriteData(buf, 4);

    /* 0x2B：行地址范围 */
    ST7735_WriteCmd(0x2B);
    buf[0] = (uint8_t)(Y1 >> 8);
    buf[1] = (uint8_t)Y1;
    buf[2] = (uint8_t)(Y2 >> 8);
    buf[3] = (uint8_t)Y2;
    ST7735_WriteData(buf, 4);

    /* 0x2C：开始写显存 */
    ST7735_WriteCmd(0x2C);
}

//
// @简介：设置背景色（画实心圆时，圆外用这个颜色填）
//
void ST7735_SetBackColor(uint16_t Color)
{
    s_bgColor = Color;
}

//
// @简介：整屏填色
//
void ST7735_Fill(uint16_t Color)
{
    ST7735_SetWindow(0, 0, s_width, s_height);

    DC_DATA();
    CS_LOW();
    ST7735_RepeatPixel(Color, (uint32_t)s_width * s_height);
    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：矩形区域填色
//
void ST7735_FillRect(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, uint16_t Color)
{
    if (Width == 0 || Height == 0) return;

    ST7735_SetWindow(X, Y, Width, Height);

    DC_DATA();
    CS_LOW();
    ST7735_RepeatPixel(Color, (uint32_t)Width * Height);
    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：画一个像素
//
void ST7735_DrawPixel(uint16_t X, uint16_t Y, uint16_t Color)
{
    ST7735_SetWindow(X, Y, 1, 1);

    DC_DATA();
    CS_LOW();
    SPI_WriteByte((uint8_t)(Color >> 8));
    SPI_WriteByte((uint8_t)Color);
    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：画实心圆
//  为了效率，这里不是"一个像素一个像素画"，而是：
//    1) 先把圆的外接矩形开成一个窗口
//    2) 逐行发送：左背景 + 圆身 + 右背景
//  这样每一行都是连续数据，发得快；而且圆外自动是背景色，
//  所以每次重画都顺手把旧位置擦干净了，不用单独清屏。
//
// @参数：CX, CY - 圆心坐标（允许超出屏幕，会自动裁剪）
// @参数：Radius - 半径
// @参数：Color  - 圆的颜色
//
void ST7735_DrawFilledCircle(int16_t CX, int16_t CY, int16_t Radius, uint16_t Color)
{
    int16_t X0, X1, Y0, Y1;
    int16_t Y, DY, HW, Left, Right;

    if (Radius <= 0) return;

    /* 先算出外接矩形，并裁到屏幕内 */
    X0 = (int16_t)(CX - Radius);
    X1 = (int16_t)(CX + Radius);
    Y0 = (int16_t)(CY - Radius);
    Y1 = (int16_t)(CY + Radius);
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > s_width  - 1) X1 = s_width  - 1;
    if (Y1 > s_height - 1) Y1 = s_height - 1;
    if (X1 < X0 || Y1 < Y0) return;          /* 完全在屏幕外 */

    ST7735_SetWindow((uint16_t)X0, (uint16_t)Y0,
                     (uint16_t)(X1 - X0 + 1), (uint16_t)(Y1 - Y0 + 1));

    DC_DATA();
    CS_LOW();

    for (Y = Y0; Y <= Y1; Y++)
    {
        DY = (int16_t)(Y - CY);
        /* 圆在这一行的一半宽度：sqrt(r^2 - dy^2) */
        HW = (int16_t)sqrtf((float)(Radius * Radius - DY * DY));

        Left  = (int16_t)(CX - HW);
        Right = (int16_t)(CX + HW);
        if (Left  < X0) Left  = X0;
        if (Right > X1) Right = X1;

        ST7735_RepeatPixel(s_bgColor, (uint32_t)(Left - X0));          /* 圆外（左） */
        ST7735_RepeatPixel(Color,      (uint32_t)(Right - Left + 1));  /* 圆身 */
        ST7735_RepeatPixel(s_bgColor, (uint32_t)(X1 - Right));         /* 圆外（右） */
    }

    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：显示一张图片
//  整张图一次开窗、一条数据流发完，比一个像素一个像素画快得多。
//
// @参数：X, Y   - 图片左上角坐标
// @参数：Width, Height - 图片宽高（像素）
// @参数：pPixels - RGB565 像素数组，按行优先：第 (行*Width + 列) 个元素
//
void ST7735_DrawImage(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height,
                      const uint16_t *pPixels)
{
    uint32_t i, n;

    if (Width == 0 || Height == 0) return;

    ST7735_SetWindow(X, Y, Width, Height);

    DC_DATA();
    CS_LOW();

    n = (uint32_t)Width * Height;
    for (i = 0; i < n; i++)
    {
        SPI_WriteByte((uint8_t)(pPixels[i] >> 8));
        SPI_WriteByte((uint8_t)pPixels[i]);
    }

    SPI_WaitIdle();
    CS_HIGH();
}

//
// @简介：把一段"已经按 高字节/低字节 排好的"像素数据直接推给屏幕
//        照片存在外部 Flash(W25Q64) 里，读出来就是这个格式，直接推最省事
// @参数：X,Y - 左上角；W,H - 尺寸；pBytes - 数据，共 W*H*2 字节
//
void ST7735_PushBytes(int16_t X, int16_t Y, uint16_t W, uint16_t H, const uint8_t *pBytes)
{
    if (W == 0 || H == 0) return;

    ST7735_SetWindow((uint16_t)X, (uint16_t)Y, W, H);

    DC_DATA();
    CS_LOW();
    SPI_DMA_SendBytes(pBytes, (uint16_t)((uint32_t)W * H * 2));
    CS_HIGH();
}

//
// @简介：初始化 ST7735
//  顺序：开时钟 -> 配 GPIO -> 配 SPI -> 硬复位 -> 灌初始化寄存器 -> 开显示
//
void ST7735_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef  SPI_InitStructure;

    /* ---- 1. 打开 GPIOA 和 SPI1 的时钟 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    /* ---- 2. PA5(SCK) / PA7(MOSI)：复用推挽 ---- */
    GPIO_InitStructure.GPIO_Pin   = ST7735_SCK_PIN | ST7735_MOSI_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ST7735_GPIO, &GPIO_InitStructure);

    /* ---- 3. PA4(CS) / PA1(DC) / PA0(RST)：普通推挽输出 ---- */
    GPIO_InitStructure.GPIO_Pin   = ST7735_CS_PIN | ST7735_DC_PIN | ST7735_RST_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ST7735_GPIO, &GPIO_InitStructure);

    CS_HIGH();
    DC_DATA();
    RST_HIGH();

    /* ---- 4. SPI1：主机 / 8 位 / 模式0(CPOL=0,CPHA=1Edge) / 软件 NSS ---- */
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = ST7735_SPI_PRESCALER;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(ST7735_SPI, &SPI_InitStructure);
    SPI_Cmd(ST7735_SPI, ENABLE);

    /* 软件 NSS 模式下，内部 NSS 必须置高，否则主机模式会误判"多主机冲突" */
    SPI_NSSInternalSoftwareConfig(ST7735_SPI, SPI_NSSInternalSoft_Set);

    ST7735_DMA_Init();                                  /* 像素数据走 DMA 推送 */

    /* ---- 5. 硬件复位 ---- */
    RST_HIGH(); Delay(10);
    RST_LOW();  Delay(20);
    RST_HIGH(); Delay(120);

    /* ---- 6. 初始化寄存器序列（1.8 寸 128x160 模块通用） ---- */
    ST7735_WriteCmd(0x01);                              /* 软复位 */
    Delay(150);

    ST7735_WriteCmd(0x11);                              /* 退出睡眠 */
    Delay(120);

    ST7735_WriteCmd(0xB1);                              /* 帧率控制（正常模式） */
    ST7735_WriteData8(0x01); ST7735_WriteData8(0x2C); ST7735_WriteData8(0x2D);
    ST7735_WriteCmd(0xB2);                              /* 帧率控制（空闲模式） */
    ST7735_WriteData8(0x01); ST7735_WriteData8(0x2C); ST7735_WriteData8(0x2D);
    ST7735_WriteCmd(0xB3);                              /* 帧率控制（局部刷新） */
    ST7735_WriteData8(0x01); ST7735_WriteData8(0x2C); ST7735_WriteData8(0x2D);
    ST7735_WriteData8(0x01); ST7735_WriteData8(0x2C); ST7735_WriteData8(0x2D);

    ST7735_WriteCmd(0xB4); ST7735_WriteData8(0x07);     /* 显示反转控制 */

    ST7735_WriteCmd(0xC0);                              /* 电源控制 1 */
    ST7735_WriteData8(0xA2); ST7735_WriteData8(0x02); ST7735_WriteData8(0x84);
    ST7735_WriteCmd(0xC1); ST7735_WriteData8(0xC5);     /* 电源控制 2 */
    ST7735_WriteCmd(0xC2); ST7735_WriteData8(0x0A); ST7735_WriteData8(0x00); /* 电源控制 3 */
    ST7735_WriteCmd(0xC3); ST7735_WriteData8(0x8A); ST7735_WriteData8(0x2A); /* 电源控制 4 */
    ST7735_WriteCmd(0xC4); ST7735_WriteData8(0x8A); ST7735_WriteData8(0xEE); /* 电源控制 5 */
    ST7735_WriteCmd(0xC5); ST7735_WriteData8(0x0E);     /* VCOM 电压 */

    ST7735_WriteCmd(0x20);                              /* 不反显 */
    ST7735_SetRotation(ST7735_ROT_PORTRAIT);            /* 显示方向 + 颜色顺序 */
    ST7735_WriteCmd(0x3A); ST7735_WriteData8(0x05);     /* 像素格式 = 16 位 RGB565 */

    ST7735_WriteCmd(0xE0); ST7735_WriteData(GAMMA_P, 16); /* 正极性 gamma */
    ST7735_WriteCmd(0xE1); ST7735_WriteData(GAMMA_N, 16); /* 负极性 gamma */

    ST7735_WriteCmd(0x13);                              /* 进入正常显示模式 */
    Delay(10);
    ST7735_WriteCmd(0x29);                              /* 打开显示 */
    Delay(100);

    ST7735_Fill(ST7735_BLACK);                          /* 上电先清成黑屏 */
}
