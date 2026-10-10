/**
  ******************************************************************************
  * @file    main.c
  * @brief   项目：多页小应用（照片页 + MPU6050 重力颗粒页 + 小恐龙跑酷页）
  * @version v1.0   (STM32F103C8T6 + 标准外设库 SPL，Keil MDK5)
  *
  * ==========================================================================
  *  一、这个项目在干什么（整体思路，先看这段再看代码）
  * ==========================================================================
  *
  *  1.8 寸彩屏上共 4 页，屏幕上不显示页码：
  *
  *     第 1 页（上电默认）：一张 128x160 的照片，竖屏，静止画面
  *     第 2 页            ：重力颗粒，竖屏，彩色小球受重力滚动、互撞
  *     第 3 页            ：小恐龙跑酷，横屏 160x128，类似 Chrome 断网小游戏
  *     第 4 页            ：太空射击，竖屏，自动开火，三种敌机 + 爆炸粒子
  *
  *  前两页是竖屏、第三页是横屏，切页时用 ST7735_SetRotation() 换方向，
  *  屏幕的宽高会跟着对调（128x160 <-> 160x128）。
  *
  * ==========================================================================
  *  二、按键怎么用（4 个键，都是一端接引脚、一端接 GND，内部上拉不用电阻）
  * ==========================================================================
  *
  *     PA2 / PA3   翻页（任何页都有效，太空射击页靠这两个键翻走）
  *     PB0         恐龙页 = 跳跃      ；太空射击页 = 左移
  *     PB1         恐龙页 = 开始/复活 ；太空射击页 = 右移
  *     双击 PB0 或 PB1 = 太空射击页的"开始 / 复活"（其它页无动作）
  *
  *  想让"哪个键干什么"换一下，只改 Buttons_Init() 里挂的回调就行。
  *
  * ==========================================================================
  *  三、小恐龙跑酷页怎么做的（第三页）
  * ==========================================================================
  *
  *  就是最经典的跑酷三件套：滚动 + 跳跃 + 碰撞检测。
  *
  *     1. 滚动：仙人掌从右往左匀速移动（x 每帧减去 速度*dt）。滚出屏幕左边的，
  *              重新排到最右边那个的后面，间隔随机，于是障碍物无限循环。
  *     2. 跳跃：按跳跃键给恐龙一个向上的初速度 vy = GAME_JUMP_V；之后每帧
  *              vy 减去 重力*dt、y 加上 vy*dt，落回地面就停住（抛物线）。
  *     3. 碰撞：恐龙和仙人掌各看成一个矩形，两个矩形有重叠就算撞上；
  *              判定框比图形略小一点，玩起来更宽容。
  *
  *  状态机：GAME_READY(等待开始) -> GAME_RUN(跑) -> GAME_DEAD(撞了) -> 按开始键复活
  *  驱动里没带字库，所以屏幕上不写字：撞死时用一个闪烁的红框提示"按开始键复活"。
  *  按你的要求，不显示跑了多久、也不显示玩了几次。
  *
  * ==========================================================================
  *  四、硬件接线（改接线只改"硬件/参数配置区"，屏幕引脚在 my_lib/st7735.h）
  * ==========================================================================
  *
  *  1.8 寸 TFT（ST7735S，SPI，模块丝印一般是 GND VCC SCL SDA RES DC CS BLK）
  *       GND        -> GND
  *       VCC        -> 3V3      （千万不要接 5V）
  *       SCL/SCK    -> PA5      SPI1 时钟
  *       SDA/MOSI   -> PA7      SPI1 数据
  *       RES/RST    -> PA0      复位
  *       DC/RS      -> PA1      命令/数据选择
  *       CS         -> PA4      片选
  *       BLK/BL     -> 3V3      背光常亮
  *
  *  MPU6050（I2C，地址 0xD0 = 7 位 0x68）
  *       VCC -> 3V3   GND -> GND   SCL -> PB6   SDA -> PB7   AD0 -> GND   INT -> 不接
  *
  *  按键（4 个，另一端统一接 GND）
  *       PA2 / PA3 / PB0 / PB1
  *
  * ==========================================================================
  *  五、参数怎么调
  * ==========================================================================
  *   重力页： GRAIN_COUNT / GRAIN_RADIUS / GRAIN_COLOR / GRAVITY_PX_PER_G /
  *            GRAV_SIGN_X / GRAV_SIGN_Y / WALL_REST / GRAIN_REST
  *   恐龙页： GAME_JUMP_V(跳多高) / GAME_JUMP_G(落多快) / GAME_OBST_SPEED(跑多快)
  *            GAME_OBST_COUNT(同时在排队的障碍物数量)
  *   换照片： python my_app/gen_photo.py <你的图片>
  *   花屏：   my_lib/st7735.h 里 ST7735_SPI_PRESCALER 改大一级
  ******************************************************************************
  */

#include "stm32f10x.h"   /* 本工程已把全部 SPL 驱动头文件包含进来 */
#include "delay.h"       /* Delay / DelayUs / GetTick */
#include "i2c.h"         /* 硬件 I2C（My_I2C_SendBytes / My_I2C_ReceiveBytes） */
#include "st7735.h"      /* 1.8 寸 TFT 彩屏驱动（my_lib） */
#include "mpu6050.h"     /* MPU6050 驱动（my_lib） */
#include "button.h"      /* 按键驱动（my_lib，内部上拉 + 消抖 + 回调） */
#include "photo.h"       /* 照片页的图片数据 */
#include "game.h"        /* 第 4 页：太空射击 */
#include <math.h>        /* sqrtf */

/* ==========================================================================
 *                        一、硬件 / 参数配置区
 * ========================================================================== */

/* ---------- MPU6050：I2C1，默认引脚 PB6(SCL) / PB7(SDA) ---------- */
#define MPU6050_I2C          I2C1
#define MPU6050_SCL_PIN      GPIO_Pin_6
#define MPU6050_SDA_PIN      GPIO_Pin_7
#define MPU6050_I2C_SPEED    400000

/* ---------- 4 个按键（内部上拉，按下接 GND） ---------- */
#define KEY_NEXT_GPIO        GPIOA          /* PA2：下一页（任何页有效） */
#define KEY_NEXT_PIN         GPIO_Pin_2
#define KEY_PREV_GPIO        GPIOA          /* PA3：上一页（任何页有效） */
#define KEY_PREV_PIN         GPIO_Pin_3
#define KEY_JUMP_GPIO        GPIOB          /* PB0：跳跃（恐龙页） */
#define KEY_JUMP_PIN         GPIO_Pin_0
#define KEY_START_GPIO       GPIOB          /* PB1：开始/复活（恐龙页） */
#define KEY_START_PIN        GPIO_Pin_1

/* ---------- 页面编号 ---------- */
#define PAGE_PHOTO           0              /* 第 1 页：照片 */
#define PAGE_GRAVITY         1              /* 第 2 页：重力颗粒 */
#define PAGE_DINO            2              /* 第 3 页：小恐龙跑酷 */
#define PAGE_SHOOTER         3              /* 第 4 页：太空射击 */
#define PAGE_COUNT           4

/* ---------- 第 2 页：重力颗粒（竖屏 128x160） ---------- */
#define SCREEN_W             ST7735_WIDTH      /* 128 */
#define SCREEN_H             ST7735_HEIGHT     /* 160 */
#define GRAIN_COUNT          72                /* 珠子数量 */
#define GRAIN_RADIUS         3                 /* 珠子半径 */
#define GRAIN_COLOR          ST7735_RGB565(0, 120, 255)  /* 珠子颜色：蓝色 */
#define BG_COLOR             ST7735_RGB565(8, 12, 32)    /* 背景：深蓝黑 */

#define GRAVITY_PX_PER_G     220.0f      /* 1g 对应的屏幕加速度（像素/秒^2） */
#define GRAV_SIGN_X          1.0f        /* 左右方向反了就改 -1.0f */
#define GRAV_SIGN_Y          1.0f        /* 上下方向反了就改 -1.0f */
#define GRAV_FILTER          0.25f       /* 重力低通滤波系数（0~1） */
#define WALL_REST            0.55f       /* 撞墙恢复系数（弹性） */
#define GRAIN_REST           0.45f       /* 珠子之间恢复系数（弹性） */
#define WALL_FRICTION        0.96f       /* 贴墙切向摩擦 */
#define MAX_DT               0.05f       /* 单帧最大步长（秒），防止穿透 */

/* ---------- 第 3 页：小恐龙跑酷（横屏 160x128） ---------- */
#define GAME_W               160         /* 横屏宽 */
#define GAME_H               128         /* 横屏高 */
#define GAME_GROUND_Y        100         /* 地面线的 y 坐标（恐龙站在这条线上） */
#define GAME_DINO_X          20          /* 恐龙固定的横向位置 */
#define GAME_DINO_W          14          /* 恐龙判定/图形宽 */
#define GAME_DINO_H          16          /* 恐龙判定/图形高 */
#define GAME_OBST_COUNT      3           /* 屏幕外同时排队的障碍物数量 */
#define GAME_JUMP_V          300.0f      /* 起跳初速度（像素/秒），越大跳越高 */
#define GAME_JUMP_G          1000.0f     /* 重力加速度（像素/秒^2），越大落得越快 */
#define GAME_OBST_SPEED      95.0f       /* 障碍物向左滚动的速度（像素/秒） */
#define GAME_GAP_MIN         70.0f       /* 重新排队的间隔下限（像素） */
#define GAME_GAP_RAND        60          /* 间隔的随机范围（像素） */

#define GAME_SKY             ST7735_RGB565(170, 215, 255)  /* 天空：浅蓝 */
#define GAME_GROUND_COLOR    ST7735_RGB565(105, 85, 60)    /* 地面：土黄 */
#define GAME_DINO_COLOR      ST7735_RGB565(70, 70, 70)     /* 恐龙：深灰 */
#define GAME_DINO_CRASH      ST7735_RGB565(220, 40, 40)    /* 撞死后恐龙变红 */
#define GAME_OBST_COLOR      ST7735_RGB565(45, 120, 55)    /* 仙人掌：绿 */
#define GAME_DANGER          ST7735_RGB565(220, 40, 40)    /* 提示红框 */

/* 游戏状态 */
#define GAME_READY           0           /* 等待开始 */
#define GAME_RUN             1           /* 跑动中 */
#define GAME_DEAD            2           /* 撞死了，等按开始键复活 */

/* ---------- 一颗珠子（第 2 页用） ---------- */
typedef struct
{
    float x, y;      /* 圆心位置（像素，用浮点便于积分） */
    float vx, vy;    /* 速度（像素/秒） */
} Grain;

/* ---------- 一个仙人掌（第 3 页用） ---------- */
typedef struct
{
    float   x;       /* 左边缘的 x 坐标（屏幕坐标，右边是 160，左边是 0） */
    uint8_t w, h;    /* 宽、高（像素） */
} Obstacle;

/* ==========================================================================
 *                        二、全局变量
 * ========================================================================== */

/* ---- 第 2 页：珠子 ---- */
static Grain g_grains[GRAIN_COUNT];

/* 当前"屏幕平面内的重力加速度"，单位 像素/秒^2。
 * 初值给 (0, +重力)，即"向下"：这样即使 MPU6050 没插好，也能看到珠子往下掉。 */
static float g_gravityX = 0.0f;
static float g_gravityY = GRAVITY_PX_PER_G;

/* ---- 第 3 页：小恐龙 ---- */
static Obstacle g_obst[GAME_OBST_COUNT];
static float    g_dinoY;                    /* 恐龙离地高度（0 = 站在地上） */
static float    g_dinoVy;                   /* 恐龙垂直速度（像素/秒） */
static uint8_t  g_gameState = GAME_READY;   /* 游戏状态 */
static uint32_t g_animTick;                 /* 腿交替动画的计时 */
static uint8_t  g_legPhase;                 /* 腿动画奇偶（0/1） */

/* ---- 公共 ---- */
static uint8_t  g_page = PAGE_PHOTO;        /* 当前在第几页，上电默认照片页 */
static uint32_t g_lastTick;                 /* 上一帧的时间（算 dt 用） */

static uint32_t g_rng = 123456789UL;        /* 伪随机数种子 */
static Button_TypeDef g_btnNext, g_btnPrev, g_btnJump, g_btnStart;

/* 简易伪随机数（线性同余），撒珠子、生成障碍物用 */
static uint32_t Rand(void)
{
    g_rng = g_rng * 1103515245UL + 12345UL;
    return (g_rng >> 16) & 0x7FFFUL;
}

/* ==========================================================================
 *                        三、I2C1（给 MPU6050 用）
 * ========================================================================== */

/* 初始化 I2C1。MPU6050 接在默认引脚 PB6/PB7 上，所以不需要重映射。 */
static void Mpu_I2c_Init(void)
{
    GPIO_InitTypeDef gpio;
    I2C_InitTypeDef  i2c;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    /* PB6=SCL, PB7=SDA，I2C 必须用复用开漏 */
    gpio.GPIO_Pin   = MPU6050_SCL_PIN | MPU6050_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    I2C_DeInit(MPU6050_I2C);

    i2c.I2C_Mode                = I2C_Mode_I2C;
    i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1         = 0x00;
    i2c.I2C_Ack                 = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed          = MPU6050_I2C_SPEED;
    I2C_Init(MPU6050_I2C, &i2c);

    I2C_Cmd(MPU6050_I2C, ENABLE);
}

/* ==========================================================================
 *                        四、第 2 页：珠子初始化
 * ========================================================================== */

/* 把珠子按网格 + 随机抖动铺开。
 * 好处：一开始互相不重叠、满屏都有，松手后就会在重力下跌落并堆到低处。 */
static void Grains_Init(void)
{
    const int cols = 8;                              /* 8 列，72 颗 -> 9 行 */
    const int rows = (GRAIN_COUNT + cols - 1) / cols;
    int       i;

    for (i = 0; i < GRAIN_COUNT; i++)
    {
        int c = i % cols;                            /* 第几列 */
        int r = i / cols;                            /* 第几行 */

        g_grains[i].x = (float)(c * (SCREEN_W / cols) + SCREEN_W / (2 * cols))
                        + (float)((int)(Rand() % 9) - 4);
        g_grains[i].y = (float)(r * (SCREEN_H / rows) + SCREEN_H / (2 * rows))
                        + (float)((int)(Rand() % 11) - 5);
        g_grains[i].vx = 0.0f;
        g_grains[i].vy = 0.0f;
    }
}

/* ==========================================================================
 *                        五、第 2 页：读倾角 -> 屏幕内重力
 * ========================================================================== */

/* 读一次 MPU6050，把加速度换算成"屏幕平面内的重力"，再做一阶低通滤波去手抖。
 * 读失败（传感器没插/通信错）就直接返回，沿用上一次的重力值，画面不中断。 */
static void Gravity_Update(void)
{
    MPU6050_Accel a;
    float         gx, gy;

    if (MPU6050_ReadAccel(MPU6050_I2C, &a) != 0) return;

    /* 这块 MPU6050 相对屏幕转了 90°，所以两个轴要对调：
     *     屏幕 X（左右） <- 传感器 Y 轴
     *     屏幕 Y（上下） <- 传感器 X 轴
     * 如果方向还是反的，就改 GRAV_SIGN_X / GRAV_SIGN_Y 的符号。 */
    gx = (float)a.ay / MPU6050_LSB_PER_G * GRAVITY_PX_PER_G * GRAV_SIGN_X;
    gy = (float)a.ax / MPU6050_LSB_PER_G * GRAVITY_PX_PER_G * GRAV_SIGN_Y;

    g_gravityX += (gx - g_gravityX) * GRAV_FILTER;
    g_gravityY += (gy - g_gravityY) * GRAV_FILTER;
}

/* ==========================================================================
 *                        六、第 2 页：物理（重力 + 珠子互撞 + 撞墙）
 * ========================================================================== */

/* 一帧物理步进
 * @参数：dt - 距上一帧的秒数（已限幅到 MAX_DT） */
static void Physics_Update(float dt)
{
    int    i, j;
    Grain *gi, *gj;

    /* --- 1. 重力加速：v += g * dt --- */
    for (i = 0; i < GRAIN_COUNT; i++)
    {
        g_grains[i].vx += g_gravityX * dt;
        g_grains[i].vy += g_gravityY * dt;
    }

    /* --- 2. 位置积分：p += v * dt --- */
    for (i = 0; i < GRAIN_COUNT; i++)
    {
        g_grains[i].x += g_grains[i].vx * dt;
        g_grains[i].y += g_grains[i].vy * dt;
    }

    /* --- 3. 珠子之间：重叠就推开，正在靠近才做弹性碰撞 --- */
    for (i = 0; i < GRAIN_COUNT; i++)
    {
        for (j = i + 1; j < GRAIN_COUNT; j++)
        {
            float dx, dy, dist2, dist, minDist, nx, ny, overlap, reln, imp;

            gi = &g_grains[i];
            gj = &g_grains[j];

            dx      = gj->x - gi->x;
            dy      = gj->y - gi->y;
            minDist = 2.0f * GRAIN_RADIUS;      /* 两球半径之和 */
            dist2   = dx * dx + dy * dy;

            if (dist2 >= minDist * minDist || dist2 < 0.0001f) continue; /* 没碰上 */

            dist = sqrtf(dist2);
            nx   = dx / dist;                   /* 从 i 指向 j 的单位法线 */
            ny   = dy / dist;

            /* 3.1 分离重叠：两颗各退一半，防止珠子"陷进去" */
            overlap = minDist - dist;
            gi->x -= nx * overlap * 0.5f;
            gi->y -= ny * overlap * 0.5f;
            gj->x += nx * overlap * 0.5f;
            gj->y += ny * overlap * 0.5f;

            /* 3.2 速度响应：只有"正在靠近"才需要处理 */
            reln = (gj->vx - gi->vx) * nx + (gj->vy - gi->vy) * ny;
            if (reln < 0.0f)
            {
                /* 等质量、恢复系数为 GRAIN_REST 的冲量大小（恒为正） */
                imp = -(1.0f + GRAIN_REST) * reln * 0.5f;

                gi->vx -= imp * nx;
                gi->vy -= imp * ny;
                gj->vx += imp * nx;
                gj->vy += imp * ny;
            }
        }
    }

    /* --- 4. 撞墙：放最后做，保证珠子一定被关在屏幕里 --- */
    for (i = 0; i < GRAIN_COUNT; i++)
    {
        Grain *g = &g_grains[i];

        if (g->x - GRAIN_RADIUS < 0.0f)                 /* 撞左墙 */
        {
            g->x  = GRAIN_RADIUS;
            g->vx = -g->vx * WALL_REST;
            g->vy *= WALL_FRICTION;
        }
        if (g->x + GRAIN_RADIUS > SCREEN_W)             /* 撞右墙 */
        {
            g->x  = SCREEN_W - GRAIN_RADIUS;
            g->vx = -g->vx * WALL_REST;
            g->vy *= WALL_FRICTION;
        }
        if (g->y - GRAIN_RADIUS < 0.0f)                 /* 撞上墙 */
        {
            g->y  = GRAIN_RADIUS;
            g->vy = -g->vy * WALL_REST;
            g->vx *= WALL_FRICTION;
        }
        if (g->y + GRAIN_RADIUS > SCREEN_H)             /* 撞下墙（地面） */
        {
            g->y  = SCREEN_H - GRAIN_RADIUS;
            g->vy = -g->vy * WALL_REST;
            g->vx *= WALL_FRICTION;
        }
    }
}

/* ==========================================================================
 *                        七、第 2 页：渲染
 * ========================================================================== */

/* 画一帧重力页：先把整屏刷成背景色，再把每颗珠子画成一个实心圆。 */
static void Gravity_Render(void)
{
    int i;

    ST7735_Fill(BG_COLOR);

    for (i = 0; i < GRAIN_COUNT; i++)
    {
        /* +0.5 是四舍五入，把浮点圆心落到最近的整数像素 */
        ST7735_DrawFilledCircle((int16_t)(g_grains[i].x + 0.5f),
                                (int16_t)(g_grains[i].y + 0.5f),
                                GRAIN_RADIUS, GRAIN_COLOR);
    }
}

/* ==========================================================================
 *                        八、第 3 页：小恐龙跑酷
 * ========================================================================== */

/* 复位到"等待开始"：恐龙落地，障碍物排到屏幕右边外面等着 */
static void Dino_Reset(void)
{
    int i;

    g_dinoY     = 0.0f;
    g_dinoVy    = 0.0f;
    g_gameState = GAME_READY;
    g_legPhase  = 0;
    g_animTick  = GetTick();

    for (i = 0; i < GAME_OBST_COUNT; i++)
    {
        g_obst[i].x = 190.0f + (float)i * 90.0f;    /* 屏幕右边外面排队 */
        g_obst[i].w = (uint8_t)(8 + Rand() % 5);    /* 宽 8~12 */
        g_obst[i].h = (uint8_t)(14 + Rand() % 14);  /* 高 14~27 */
    }
}

/* 按"开始/复活"键：等待中或撞死后都能开始 */
static void Dino_Start(void)
{
    if (g_gameState == GAME_RUN) return;    /* 正在跑，不用理会 */

    Dino_Reset();
    g_gameState = GAME_RUN;
}

/* 按"跳跃"键：只有站在地上才能起跳（空中不能连跳） */
static void Dino_Jump(void)
{
    if (g_gameState != GAME_RUN) return;
    if (g_dinoY > 0.0f) return;

    g_dinoVy = GAME_JUMP_V;
}

/* 碰撞检测：恐龙和每个仙人掌的矩形只要有重叠就算撞上
 * @返回值：1 - 撞了；0 - 安全 */
static uint8_t Dino_Hit(void)
{
    int   i;
    float dx0, dx1, dy0, dy1;      /* 恐龙的判定框 */
    float ox0, ox1, oy0, oy1;      /* 仙人掌的判定框 */

    /* 判定框左右各缩 2 像素、上下各缩 1 像素，玩起来宽容一点 */
    dx0 = (float)(GAME_DINO_X + 2);
    dx1 = (float)(GAME_DINO_X + GAME_DINO_W - 2);
    dy0 = (float)(GAME_GROUND_Y - GAME_DINO_H) - g_dinoY + 1.0f;
    dy1 = (float)GAME_GROUND_Y - g_dinoY;

    for (i = 0; i < GAME_OBST_COUNT; i++)
    {
        ox0 = g_obst[i].x;
        ox1 = g_obst[i].x + (float)g_obst[i].w;
        oy0 = (float)(GAME_GROUND_Y - g_obst[i].h);
        oy1 = (float)GAME_GROUND_Y;

        if (dx0 < ox1 && dx1 > ox0 && dy0 < oy1 && dy1 > oy0)
        {
            return 1;
        }
    }
    return 0;
}

/* 一帧游戏逻辑
 * @参数：dt - 距上一帧的秒数（已限幅到 MAX_DT） */
static void Dino_Update(float dt)
{
    int   i, j;
    float maxX;

    if (g_gameState == GAME_RUN)
    {
        /* --- 1. 障碍物往左滚 --- */
        for (i = 0; i < GAME_OBST_COUNT; i++)
        {
            g_obst[i].x -= GAME_OBST_SPEED * dt;

            /* 完全滚出左边后，排到"最右边那个"后面重新进场（间隔随机） */
            if (g_obst[i].x + (float)g_obst[i].w < 0.0f)
            {
                maxX = 0.0f;
                for (j = 0; j < GAME_OBST_COUNT; j++)
                {
                    if (g_obst[j].x > maxX) maxX = g_obst[j].x;
                }

                g_obst[i].x = maxX + GAME_GAP_MIN + (float)(Rand() % GAME_GAP_RAND);
                g_obst[i].w = (uint8_t)(8 + Rand() % 5);
                g_obst[i].h = (uint8_t)(14 + Rand() % 14);
            }
        }

        /* --- 2. 跳跃（简单的抛物线运动） --- */
        g_dinoVy -= GAME_JUMP_G * dt;
        g_dinoY  += g_dinoVy * dt;
        if (g_dinoY <= 0.0f)                 /* 落回地面 */
        {
            g_dinoY  = 0.0f;
            g_dinoVy = 0.0f;
        }

        /* --- 3. 撞了没有 --- */
        if (Dino_Hit()) g_gameState = GAME_DEAD;
    }

    /* --- 4. 跑动时两条腿交替，做最简单的逐帧动画 --- */
    if (g_gameState == GAME_RUN && (GetTick() - g_animTick) >= 100)
    {
        g_animTick = GetTick();
        g_legPhase ^= 1;
    }
}

/* 画一帧恐龙页（横屏 160x128） */
static void Dino_Render(void)
{
    int      i;
    int16_t  bottom = (int16_t)(GAME_GROUND_Y - g_dinoY);   /* 恐龙脚底所在的行 */
    int16_t  top    = (int16_t)(bottom - GAME_DINO_H);      /* 恐龙头顶所在的行 */
    int16_t  leg1   = GAME_DINO_H - 12;                     /* 腿长（默认伸直 = 4） */
    int16_t  leg2   = GAME_DINO_H - 12;
    uint16_t dinoColor = (g_gameState == GAME_DEAD) ? GAME_DINO_CRASH : GAME_DINO_COLOR;

    /* 天空 + 地面 */
    ST7735_FillRect(0, 0, GAME_W, GAME_GROUND_Y, GAME_SKY);
    ST7735_FillRect(0, GAME_GROUND_Y, GAME_W, GAME_H - GAME_GROUND_Y, GAME_GROUND_COLOR);

    /* 仙人掌：从地面往上长，画之前先把屏幕外的那部分裁掉 */
    for (i = 0; i < GAME_OBST_COUNT; i++)
    {
        int16_t ox = (int16_t)g_obst[i].x;
        int16_t ow = (int16_t)g_obst[i].w;
        int16_t oh = (int16_t)g_obst[i].h;

        if (ox + ow <= 0 || ox >= GAME_W) continue;    /* 完全在屏幕外 */
        if (ox < 0) { ow += ox; ox = 0; }              /* 左边露出来一半 */
        if (ox + ow > GAME_W) ow = GAME_W - ox;        /* 右边露出半截 */
        if (ow <= 0) continue;

        ST7735_FillRect((uint16_t)ox, (uint16_t)(GAME_GROUND_Y - oh),
                        (uint16_t)ow, (uint16_t)oh, GAME_OBST_COLOR);
    }

    /* 恐龙：站在地上跑动时两条腿一长一短，做出"跑步"的感觉 */
    if (g_gameState == GAME_RUN && g_dinoY <= 0.0f)
    {
        if (g_legPhase) { leg1 = 2; leg2 = 4; }
        else            { leg1 = 4; leg2 = 2; }
    }

    ST7735_FillRect(GAME_DINO_X + 5, (uint16_t)(top + 0), 9, 7, dinoColor);               /* 头 */
    ST7735_FillRect(GAME_DINO_X + 0, (uint16_t)(top + 5), 10, 7, dinoColor);              /* 身体 */
    ST7735_FillRect(GAME_DINO_X + 1, (uint16_t)(top + 12), 3, (uint16_t)leg1, dinoColor); /* 左腿 */
    ST7735_FillRect(GAME_DINO_X + 7, (uint16_t)(top + 12), 3, (uint16_t)leg2, dinoColor); /* 右腿 */
    ST7735_FillRect(GAME_DINO_X + 11, (uint16_t)(top + 2), 2, 2, GAME_SKY);               /* 眼睛 */

    /* 撞死了没有字库可以写字，就用一个闪烁的红框提示"按开始键复活" */
    if (g_gameState == GAME_DEAD && ((GetTick() / 250) & 1))
    {
        ST7735_FillRect(0, 0, GAME_W, 3, GAME_DANGER);
        ST7735_FillRect(0, GAME_H - 3, GAME_W, 3, GAME_DANGER);
        ST7735_FillRect(0, 0, 3, GAME_H, GAME_DANGER);
        ST7735_FillRect(GAME_W - 3, 0, 3, GAME_H, GAME_DANGER);
    }
}

/* 恐龙页的一帧：算时间 -> 更新 -> 重画 */
static void Dino_Loop(void)
{
    uint32_t now = GetTick();
    float    dt  = (now - g_lastTick) / 1000.0f;

    if (dt < 0.001f) return;        /* 时间还没走够，先不计算 */
    if (dt > MAX_DT) dt = MAX_DT;   /* 限幅，防止卡顿后一步跨太大 */
    g_lastTick = now;

    Dino_Update(dt);
    Dino_Render();
}

/* ==========================================================================
 *                        九、第 1 页：照片
 * ==========================================================================
 *  照片用 256 色调色板存：调色板 512 字节 + 每像素 1 字节下标，
 *  128x160 一共 20992 字节（约 20.5KB）。直接存 RGB565 要 40KB，
 *  而单片机内部 Flash 一共 64KB，所以用调色板省一半，画质几乎看不出。
 *  数据在 user/photo.c，换照片执行：python my_app/gen_photo.py <图片>
 * ========================================================================== */

/* 把照片显示出来：逐行把"调色板下标"翻成 RGB565，再推给屏幕 */
static void Photo_Show(void)
{
    static uint8_t row[PHOTO_WIDTH * 2];
    const uint8_t *p;
    uint16_t y, x, c;

    for (y = 0; y < PHOTO_HEIGHT; y++)
    {
        p = &PHOTO_IDX[(uint32_t)y * PHOTO_WIDTH];

        for (x = 0; x < PHOTO_WIDTH; x++)
        {
            c = PHOTO_PAL[p[x]];
            row[x * 2]     = (uint8_t)(c >> 8);
            row[x * 2 + 1] = (uint8_t)c;
        }

        ST7735_PushBytes(0, (int16_t)y, PHOTO_WIDTH, 1, row);
    }
}
/* ==========================================================================
 *                        十、页面切换 + 按键
 * ========================================================================== */

/* 画当前这一页（顺便把屏幕方向切成这一页需要的方向） */
static void Page_Draw(void)
{
    if (g_page == PAGE_PHOTO)
    {
        ST7735_SetRotation(ST7735_ROT_PORTRAIT);            /* 竖屏 128x160 */
        Photo_Show();
    }
    else if (g_page == PAGE_GRAVITY)
    {
        ST7735_SetRotation(ST7735_ROT_PORTRAIT);            /* 竖屏 128x160 */
        g_lastTick = GetTick();
        Gravity_Render();
    }
    else if (g_page == PAGE_DINO)
    {
        ST7735_SetRotation(ST7735_ROT_LANDSCAPE);           /* 横屏 160x128 */
        Dino_Reset();
        g_lastTick = GetTick();
        Dino_Render();
    }
    else /* PAGE_SHOOTER */
    {
        ST7735_SetRotation(ST7735_ROT_PORTRAIT);            /* 竖屏 128x160 */
        Shooter_Enter();                                    /* 复位到标题界面并画一帧 */
    }
}

/* 翻页
 * @参数：delta - +1 下一页，-1 上一页（越界自动绕回来） */
static void Page_Switch(int8_t delta)
{
    int p = (int)g_page + delta;

    while (p < 0)           p += PAGE_COUNT;
    while (p >= PAGE_COUNT) p -= PAGE_COUNT;

    g_page = (uint8_t)p;
    Page_Draw();
}

/* ---- 按键回调 ---- */

/* PA2 / PA3：翻页，任何页都有效（太空射击页靠这两个键翻走） */
static void Btn_Next(void) { Page_Switch(+1); }
static void Btn_Prev(void) { Page_Switch(-1); }

/* PB0：恐龙页 = 跳跃；太空射击页 = 左移 */
static void Btn_Jump(void)
{
    if (g_page == PAGE_DINO)         Dino_Jump();
    else if (g_page == PAGE_SHOOTER) Shooter_Left();
}
static void Btn_JumpRel(void)
{
    if (g_page == PAGE_SHOOTER) Shooter_LeftRelease();
}

/* PB1：恐龙页 = 开始/复活；太空射击页 = 右移 */
static void Btn_Start(void)
{
    if (g_page == PAGE_DINO)         Dino_Start();
    else if (g_page == PAGE_SHOOTER) Shooter_Right();
}
static void Btn_StartRel(void)
{
    if (g_page == PAGE_SHOOTER) Shooter_RightRelease();
}

/* 双击任意一个游戏键 = 太空射击页的"开始 / 复活"（其它页无动作） */
static void Btn_GameClick(uint8_t clicks)
{
    if (g_page == PAGE_SHOOTER && clicks >= 2) Shooter_Start();
}

/* 初始化 4 个按键（内部上拉，按下接到 GND 读低） */
static void Buttons_Init(void)
{
    Button_InitTypeDef cfg;

    /* 这里只用"按下"回调，其它功能（单击/长按）关掉 */
    cfg.button_pressed_cb      = 0;
    cfg.button_released_cb     = 0;
    cfg.button_clicked_cb      = 0;
    cfg.button_long_pressed_cb = 0;
    cfg.LongPressTime          = 0;
    cfg.LongPressTickInterval  = 0;
    cfg.ClickInterval          = 0;

    cfg.GPIOx = KEY_NEXT_GPIO;  cfg.GPIO_Pin = KEY_NEXT_PIN;  cfg.button_pressed_cb = Btn_Next;  cfg.button_released_cb = 0;
    My_Button_Init(&g_btnNext, &cfg);

    cfg.GPIOx = KEY_PREV_GPIO;  cfg.GPIO_Pin = KEY_PREV_PIN;  cfg.button_pressed_cb = Btn_Prev;  cfg.button_released_cb = 0;
    My_Button_Init(&g_btnPrev, &cfg);

    cfg.GPIOx = KEY_JUMP_GPIO;  cfg.GPIO_Pin = KEY_JUMP_PIN;  cfg.button_pressed_cb = Btn_Jump;  cfg.button_released_cb = Btn_JumpRel;
    cfg.button_clicked_cb = Btn_GameClick;
    My_Button_Init(&g_btnJump, &cfg);

    cfg.GPIOx = KEY_START_GPIO; cfg.GPIO_Pin = KEY_START_PIN; cfg.button_pressed_cb = Btn_Start; cfg.button_released_cb = Btn_StartRel;
    cfg.button_clicked_cb = Btn_GameClick;
    My_Button_Init(&g_btnStart, &cfg);
}

/* ==========================================================================
 *                        十一、main
 * ========================================================================== */

int main(void)
{
    /* ---- 1. 外设初始化 ---- */
    Delay_Init();
    Mpu_I2c_Init();          /* I2C1：PB6/PB7，给 MPU6050 用 */
    ST7735_Init();           /* SPI1 + ST7735：上电自带硬复位和清屏 */
    ST7735_SetBackColor(BG_COLOR);
    Buttons_Init();          /* PA2 / PA3 / PB0 / PB1 */

    /* ---- 2. 传感器 + 珠子 ---- */
    MPU6050_Init(MPU6050_I2C);   /* 失败也不死机：重力退回默认的"向下" */
    g_rng = GetTick();           /* 用开机时间做随机种子 */
    Grains_Init();

    /* ---- 4. 显示第一页 ---- */
    Page_Draw();

    /* ---- 5. 主循环 ---- */
    while (1)
    {
        /* 轮询 4 个按键（消抖 + 回调都在里面） */
        My_Button_Proc(&g_btnNext);
        My_Button_Proc(&g_btnPrev);
        My_Button_Proc(&g_btnJump);
        My_Button_Proc(&g_btnStart);

        if (g_page == PAGE_GRAVITY)
        {
            /* 重力页：每帧算物理、重画 */
            uint32_t now = GetTick();
            float    dt  = (now - g_lastTick) / 1000.0f;

            if (dt < 0.001f) continue;
            if (dt > MAX_DT) dt = MAX_DT;
            g_lastTick = now;

            Gravity_Update();
            Physics_Update(dt);
            Gravity_Render();
        }
        else if (g_page == PAGE_DINO)
        {
            /* 恐龙页：每帧跑游戏逻辑、重画 */
            Dino_Loop();
        }
        else if (g_page == PAGE_SHOOTER)
        {
            /* 太空射击页：状态机 + 脏矩形局部刷新 */
            Shooter_Loop();
        }
        /* 照片页是静止画面，画一次就行，这里什么都不用做 */
    }
}
