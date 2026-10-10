/**
  ******************************************************************************
  * @file    game.c
  * @brief   太空射击（第 4 页）—— 竖屏 128x160，标准外设库，无 HAL/LVGL
  *
  * ==========================================================================
  *  一、渲染思路（为什么这样写）
  * ==========================================================================
  *
  *  F103C8T6 只有 20KB RAM，**放不下整屏 40KB 的显存**，所以这块屏没有双缓冲，
  *  也不能整屏重绘。这里用的是嵌入式小游戏常用的"脏矩形 + 逐行合成"：
  *
  *    1. 每帧先算出"哪些小矩形区域变了"（移动物体的旧位置 + 新位置）；
  *    2. 只对这些矩形重新画：逐行在行缓冲里先铺背景（星空），再叠上粒子、
  *       爆炸、敌机、子弹、飞船，最后用 DMA 把这一行推给屏幕；
  *    3. 星空滚动比较特殊 —— 它每 200ms 才跳 1 像素，跳的时候把整个游戏区
  *       标成脏矩形重画一次，其余帧只画物体的小矩形。
  *
  *  这样 25 帧/秒里有 24 帧只刷几个小方块，只有 5 帧/秒会整块重画一次背景。
  *
  * ==========================================================================
  *  二、数值约定
  * ==========================================================================
  *   - 屏幕上的一切坐标都是整数像素（避免浮点，Cortex-M3 没有 FPU）；
  *   - 速度和位置用 8.8 定点（低 8 位是小数），画的时候 >>8，这样子弹/敌机
  *     移动是平滑的，不会一帧跳 7 个像素；
  *   - 对象全部用固定数组（对象池），不使用 malloc。
  *
  * ==========================================================================
  *  三、状态机与操作（3 个键：PB0 左 / PB1 右 / PA2 开始·复活）
  * ==========================================================================
  *     MENU      标题界面，按 PA2 开始
  *     PLAYING   跑分；PB0/PB1 左右移动，**自动开火**（没有开火键）
  *     GAME_OVER 生命打光后进入，按 PA2 复活重来
  *     PA3 仍然是"上一页"，靠它翻走离开这一页
  ******************************************************************************
  */

#include "game.h"
#include "st7735.h"
#include "sprites.h"
#include "delay.h"

/* ==========================================================================
 *                        一、配置
 * ========================================================================== */

#define HUD_H            16                  /* 顶部 HUD 高度 */
#define PLAY_Y0          HUD_H               /* 游戏区上边界 */
#define PLAY_Y1          (ST7735_HEIGHT - 1)/* 游戏区下边界 */
#define PLAY_H           (ST7735_HEIGHT - HUD_H)
#define SCR_W            ST7735_WIDTH

#define PLAYER_Y         142                 /* 飞船固定的 y（左上角） */
#define PLAYER_SPEED     78                  /* 左右移动速度（像素/秒） */
#define FIRE_PERIOD_MS   165                 /* 自动开火间隔 */
#define BULLET_VY        (-210)              /* 我方子弹速度（像素/秒，向上） */
#define EBULLET_VY       64                  /* 敌方子弹速度（像素/秒，向下） */

#define MAX_BULLETS      10
#define MAX_ENEMIES      6
#define MAX_EBULLETS     6
#define MAX_PARTS        36
#define MAX_BOOMS        4
#define MAX_STARS        42
#define MAX_DIRTY        12

#define SCROLL_TICK_MS   200                 /* 星空每 200ms 滚 1 像素 */
#define SPAWN_MS         900                 /* 敌机出场间隔 */
#define SHAKE_FRAMES     7                   /* 屏幕震动帧数 */
#define FLASH_FRAMES     2                   /* 受击闪白帧数 */
#define INVUL_FRAMES     70                  /* 复活无敌帧数 */

/* 颜色（深空配色：深蓝黑底 + 霓虹青/紫/橙） */
#define C_SPACE      ST7735_RGB565(4, 6, 22)
#define C_HUD        ST7735_RGB565(8, 10, 28)
#define C_CYAN       ST7735_RGB565(80, 240, 255)
#define C_YELLOW     ST7735_RGB565(255, 220, 60)
#define C_RED        ST7735_RGB565(235, 60, 60)
#define C_GREEN      ST7735_RGB565(70, 235, 130)
#define C_BARBGD     ST7735_RGB565(26, 34, 60)
#define C_DIM        ST7735_RGB565(90, 120, 160)
#define C_SHIELD     ST7735_RGB565(60, 200, 255)
#define C_SHIELD_LO  ST7735_RGB565(255, 120, 40)
#define C_FLASH      ST7735_RGB565(255, 200, 200)

static const uint16_t STAR_COLOR[3] =
{
    ST7735_RGB565(70, 90, 130),      /* 0 远景：暗 */
    ST7735_RGB565(140, 175, 220),    /* 1 中景 */
    ST7735_RGB565(240, 250, 255)     /* 2 近景：亮 */
};

/* ==========================================================================
 *                        二、对象池
 * ========================================================================== */

typedef struct { int32_t x8, y8; uint8_t on; } Bullet;          /* 8.8 定点 */
typedef struct { int32_t x8, y8; uint8_t on, type, hp, hit; } Enemy;
typedef struct { int16_t x, y, vx, vy; uint8_t life, max; uint16_t col; } Particle;
typedef struct { int16_t x, y; uint8_t frame, tick; } Boom;
typedef struct { int16_t x, base; uint8_t layer; } Star;

static Bullet   g_bullets[MAX_BULLETS];
static Enemy    g_enemies[MAX_ENEMIES];
static Bullet   g_ebullets[MAX_EBULLETS];
static Particle g_parts[MAX_PARTS];
static Boom     g_booms[MAX_BOOMS];
static Star     g_stars[MAX_STARS];

/* 三种敌机：侦察机（快、1 血）、战斗机（中、2 血）、精英机（慢、3 血） */
static const uint8_t ENEMY_W[3]     = { SPR_SCOUT_W, SPR_FIGHTER_W, SPR_ELITE_W };
static const uint8_t ENEMY_H[3]     = { SPR_SCOUT_H, SPR_FIGHTER_H, SPR_ELITE_H };
static const uint8_t ENEMY_HP[3]    = { 1, 2, 3 };
static const int16_t ENEMY_VY[3]    = { 40, 26, 16 };
static const uint16_t ENEMY_SCORE[3]= { 10, 25, 60 };
static const uint8_t *const ENEMY_ART[3] = { SPR_SCOUT, SPR_FIGHTER, SPR_ELITE };

/* ==========================================================================
 *                        三、游戏状态
 * ========================================================================== */

#define ST_MENU      0                    /* 标题界面：等"开始"键 */
#define ST_PLAY      1                    /* 游戏中 */
#define ST_OVER      2                    /* 撞光了：等"开始"键复活 */

static uint8_t  g_state;
static uint32_t g_lastTick;       /* 上一帧时间 */
static uint32_t g_fireTick;       /* 上次开火时间 */
static uint32_t g_scrollTick;     /* 星空滚动节拍计时 */
static uint32_t g_spawnTick;      /* 敌机出场计时 */
static uint16_t g_tickCnt;        /* 星空滚动拍数 */

static int32_t  g_px8;            /* 飞船 x（8.8 定点） */
static uint8_t  g_lives;          /* 剩余命数 */
static uint8_t  g_shield;         /* 护盾（0~100） */
static uint8_t  g_tilt;           /* 0 正常 1 左倾 2 右倾 */
static uint8_t  g_holdL, g_holdR; /* 左右键是否按住 */
static int16_t  g_shake;          /* 震动剩余帧 */
static int16_t  g_flash;          /* 闪白剩余帧 */
static int16_t  g_invul;          /* 无敌剩余帧 */
static uint32_t g_score, g_hiscore;

static char     g_hudS[9];        /* "SC000123" */
static char     g_hudH[9];        /* "HI000456" */
static uint8_t  g_hudDirty;

static uint32_t g_rng;

/* ==========================================================================
 *                        四、小工具
 * ========================================================================== */

static uint32_t Rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* 把数字写成固定位数的十进制（不含结尾 0） */
static void FmtNum(char *p, uint32_t v, uint8_t digits)
{
    int8_t i;
    for (i = (int8_t)(digits - 1); i >= 0; i--) { p[i] = (char)('0' + v % 10); v /= 10; }
    p[digits] = 0;
}

static void Hud_Text(void)
{
    g_hudS[0] = 'S'; g_hudS[1] = 'C'; FmtNum(&g_hudS[2], g_score, 6);
    g_hudH[0] = 'H'; g_hudH[1] = 'I'; FmtNum(&g_hudH[2], g_hiscore, 6);
}

/* ==========================================================================
 *                        五、脏矩形
 * ========================================================================== */

static int16_t g_dr[MAX_DIRTY][4];   /* x0,y0,x1,y1 */
static uint8_t g_drCount;
static uint8_t g_full;               /* 1 = 这一帧整个游戏区都要重画 */

static void Dirty_Add(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t t;

    if (g_full) return;                       /* 反正要整块重画，不用记了 */
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W - 1)    x1 = SCR_W - 1;
    if (y1 > ST7735_HEIGHT - 1) y1 = ST7735_HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return;

    if (g_drCount >= MAX_DIRTY) { g_full = 1; return; }   /* 太多就干脆整块重画 */

    g_dr[g_drCount][0] = x0; g_dr[g_drCount][1] = y0;
    g_dr[g_drCount][2] = x1; g_dr[g_drCount][3] = y1;
    g_drCount++;
}

/* ==========================================================================
 *                        六、逐行渲染
 * ========================================================================== */

static void Line_Sprite(int16_t x, int16_t y, uint8_t w, uint8_t h,
                        const uint8_t *art, uint16_t tint)
{
    GFX_LineSpriteRow(x, y, w, h, art, SPRITE_PALETTE, tint);
}

/* 星星在这一帧的屏幕 y：每层速度不同（1 : 1/2 : 1/4），做出视差 */
static int16_t Star_Y(uint8_t i)
{
    uint8_t  L = g_stars[i].layer;
    int16_t  off;

    if (L == 2)      off = (int16_t)g_tickCnt;
    else if (L == 1) off = (int16_t)(g_tickCnt >> 1);
    else             off = (int16_t)(g_tickCnt >> 2);

    off = (int16_t)((g_stars[i].base + off) % PLAY_H);
    return (int16_t)(PLAY_Y0 + off);
}

/* 画一行星空背景 */
static void Bg_Line(int16_t y)
{
    uint8_t i;

    GFX_LineFill(C_SPACE);

    for (i = 0; i < MAX_STARS; i++)
    {
        if (Star_Y(i) == y) GFX_LinePixel(g_stars[i].x, STAR_COLOR[g_stars[i].layer]);
    }
}

/* 画一行 HUD */
static void Hud_Line(int16_t y)
{
    GFX_LineFill(C_HUD);

    if (y < 8)
    {
        GFX_LineTextRow(0, 0, g_hudS, C_CYAN);
        GFX_LineTextRow(64, 0, g_hudH, C_YELLOW);
    }
    else
    {
        /* 护盾条：x 2~93，y 10~13 */
        if (y >= 10 && y <= 13)
        {
            int16_t x, fill = (int16_t)(g_shield * 92 / 100);
            uint16_t c = (g_shield <= 30) ? C_SHIELD_LO : C_SHIELD;
            for (x = 0; x < 92; x++) GFX_LinePixel((int16_t)(2 + x), (x < fill) ? c : C_BARBGD);
        }
        /* 剩余命数：小菱形图标，最多画 3 个 */
        if (y >= 10 && y <= 14)
        {
            static const uint8_t ICON[5] = { 0x04, 0x0E, 0x1F, 0x0E, 0x04 };
            uint8_t n, b, m, i;
            b = ICON[y - 10];
            for (n = 0; n < g_lives && n < 3; n++)
            {
                i = 0;
                for (m = 0x10; m; m >>= 1, i++) if (b & m) GFX_LinePixel((int16_t)(100 + n * 10 + i), C_CYAN);
            }
        }
    }
}

/* 画一行游戏物体（顺序就是图层顺序：后画的盖住先画的） */
static void Obj_Line(int16_t y, int16_t dy)
{
    uint8_t i;
    int16_t x, py;

    /* 粒子（爆炸碎屑、尾焰） */
    for (i = 0; i < MAX_PARTS; i++)
    {
        if (g_parts[i].life == 0) continue;
        py = (int16_t)(g_parts[i].y + dy);
        if (py == y) GFX_LinePixel(g_parts[i].x, g_parts[i].col);
    }

    /* 爆炸精灵 */
    for (i = 0; i < MAX_BOOMS; i++)
    {
        if (g_booms[i].frame >= BOOM_FRAMES) continue;
        Line_Sprite(g_booms[i].x, (int16_t)(g_booms[i].y + dy), BOOM_W, BOOM_H,
                    SPR_BOOM[g_booms[i].frame], 0);
    }

    /* 敌机（被打中时整只闪白） */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (!g_enemies[i].on) continue;
        x = (int16_t)((g_enemies[i].x8 >> 8));
        py = (int16_t)((g_enemies[i].y8 >> 8) + dy);
        Line_Sprite(x, py, ENEMY_W[g_enemies[i].type], ENEMY_H[g_enemies[i].type],
                    ENEMY_ART[g_enemies[i].type], g_enemies[i].hit ? ST7735_WHITE : 0);
    }

    /* 敌方子弹 */
    for (i = 0; i < MAX_EBULLETS; i++)
    {
        if (!g_ebullets[i].on) continue;
        Line_Sprite((int16_t)(g_ebullets[i].x8 >> 8), (int16_t)((g_ebullets[i].y8 >> 8) + dy),
                    BULLET_E_W, BULLET_E_H, SPR_BULLET_E, 0);
    }

    /* 我方子弹 */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (!g_bullets[i].on) continue;
        Line_Sprite((int16_t)(g_bullets[i].x8 >> 8), (int16_t)((g_bullets[i].y8 >> 8) + dy),
                    BULLET_P_W, BULLET_P_H, SPR_BULLET_P, 0);
    }

    /* 飞船（无敌时每隔几帧闪一下） */
    if (g_invul == 0 || ((g_invul >> 1) & 1))
    {
        x = (int16_t)(g_px8 >> 8);
        Line_Sprite(x, (int16_t)(PLAYER_Y + dy), PLAYER_W, PLAYER_H,
                    SPR_PLAYER[g_tilt], 0);
    }

    /* 引擎尾焰：两条，长度每帧交替，做出喷射闪烁 */
    {
        int16_t fy0 = (int16_t)(PLAYER_Y + PLAYER_H + dy);
        if (y >= fy0 && y <= (int16_t)(fy0 + 4))
        {
            static const uint8_t FL[5] = { 0x22, 0x36, 0x3E, 0x1C, 0x08 };
            uint8_t r = (uint8_t)(y - fy0);
            uint8_t b = (uint8_t)((g_tickCnt & 1) ? FL[r] : (FL[r] >> 1));  /* 交替 -> 闪烁 */
            uint8_t k;
            int16_t bx = (int16_t)(g_px8 >> 8);

            for (k = 0; k < 6; k++)
            {
                if (b & (0x20 >> k))
                {
                    GFX_LinePixel((int16_t)(bx + 3 + k),
                                  (r < 2) ? ST7735_RGB565(255, 230, 140)
                                          : ST7735_RGB565(255, 130, 40));
                }
            }
        }
    }
}

/* 标题 / 结束界面：只画字（背景星空照旧） */
static void Overlay_Line(int16_t y)
{
    (void)y;    /* 行号由 GFX_LineTextRow 自己判断，这里不需要 */

    if (g_state == ST_MENU)
    {
        GFX_LineTextRow(44, 44, "SPACE",    C_CYAN);
        GFX_LineTextRow(36, 56, "SHOOTER",  C_YELLOW);
        GFX_LineTextRow(28, 78, "AUTO FIRE", C_DIM);
        GFX_LineTextRow(12, 90, "PB0 LEFT  PB1 RIGHT", C_DIM);
        GFX_LineTextRow(12, 108, "PA2 = START", C_CYAN);
    }
    else /* ST_OVER */
    {
        GFX_LineTextRow(36, 62, "GAME OVER",   C_RED);
        GFX_LineTextRow(12, 82, "PRESS PA2 AGAIN", C_CYAN);
    }
}

/* 重画一个矩形区域 */
static void RedrawRect(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t y;
    int16_t dy = g_shake ? ((g_tickCnt & 1) ? 1 : -1) : 0;   /* 震动：整场上下抖 1 像素 */

    for (y = y0; y <= y1; y++)
    {
        GFX_LineStart(x0, x1, y);

        if (y < HUD_H)
        {
            Hud_Line(y);
        }
        else
        {
            if (g_flash) GFX_LineFill(C_FLASH);      /* 受击闪白 */
            else
            {
                Bg_Line(y);
                if (g_state == ST_PLAY) Obj_Line(y, dy);
                else                    Overlay_Line(y);
            }
        }

        GFX_LineFlush();
    }
}

/* 把这一帧攒下来的脏矩形合并后逐个重画 */
static void Dirty_Flush(void)
{
    uint8_t i, j;

    if (g_full || g_drCount == 0)
    {
        if (g_full)
        {
            RedrawRect(0, 0, SCR_W - 1, HUD_H - 1);
            RedrawRect(0, PLAY_Y0, SCR_W - 1, PLAY_Y1);
        }
        return;
    }

    /* 先粗暴合并：谁和谁挨着就并成一个（n 很小，直接两两比较） */
    for (i = 0; i < g_drCount; i++)
    {
        if (g_dr[i][0] > g_dr[i][2]) continue;

        for (j = (uint8_t)(i + 1); j < g_drCount; j++)
        {
            if (g_dr[j][0] > g_dr[j][2]) continue;

            if (!(g_dr[j][0] > g_dr[i][2] + 2 || g_dr[j][2] < g_dr[i][0] - 2 ||
                  g_dr[j][1] > g_dr[i][3] + 2 || g_dr[j][3] < g_dr[i][1] - 2))
            {
                if (g_dr[j][0] < g_dr[i][0]) g_dr[i][0] = g_dr[j][0];
                if (g_dr[j][1] < g_dr[i][1]) g_dr[i][1] = g_dr[j][1];
                if (g_dr[j][2] > g_dr[i][2]) g_dr[i][2] = g_dr[j][2];
                if (g_dr[j][3] > g_dr[i][3]) g_dr[i][3] = g_dr[j][3];
                g_dr[j][0] = 1; g_dr[j][2] = 0;          /* 标记已合并 */
            }
        }
    }

    for (i = 0; i < g_drCount; i++)
    {
        if (g_dr[i][0] > g_dr[i][2]) continue;
        RedrawRect(g_dr[i][0], g_dr[i][1], g_dr[i][2], g_dr[i][3]);
    }
}

/* ==========================================================================
 *                        七、游戏逻辑
 * ========================================================================== */

static void Enemy_Box(uint8_t i, int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    *x = (int16_t)(g_enemies[i].x8 >> 8);
    *y = (int16_t)(g_enemies[i].y8 >> 8);
    *w = ENEMY_W[g_enemies[i].type];
    *h = ENEMY_H[g_enemies[i].type];
}

/* 生成一团粒子（爆炸碎屑 / 引擎火星） */
static void Parts_Spawn(int16_t x, int16_t y, uint8_t n, int16_t spread, uint16_t c1, uint16_t c2)
{
    uint8_t i, k;
    for (i = 0, k = 0; i < MAX_PARTS && k < n; i++)
    {
        if (g_parts[i].life) continue;
        g_parts[i].x    = x;
        g_parts[i].y    = y;
        g_parts[i].vx   = (int16_t)((int16_t)(Rnd() % (2 * spread + 1)) - spread);
        g_parts[i].vy   = (int16_t)((int16_t)(Rnd() % (2 * spread + 1)) - spread);
        g_parts[i].life = (uint8_t)(10 + Rnd() % 14);
        g_parts[i].max  = g_parts[i].life;
        g_parts[i].col  = (Rnd() & 1) ? c1 : c2;
        k++;
    }
}

static void Boom_Add(int16_t x, int16_t y)
{
    uint8_t i;
    for (i = 0; i < MAX_BOOMS; i++)
    {
        if (g_booms[i].frame >= BOOM_FRAMES)
        {
            g_booms[i].x = (int16_t)(x - BOOM_W / 2);
            g_booms[i].y = (int16_t)(y - BOOM_H / 2);
            g_booms[i].frame = 0;
            g_booms[i].tick = 0;
            return;
        }
    }
}

/* 敌机被打死 */
static void Enemy_Kill(uint8_t i)
{
    int16_t x, y, w, h;

    Enemy_Box(i, &x, &y, &w, &h);
    Boom_Add((int16_t)(x + w / 2), (int16_t)(y + h / 2));
    Parts_Spawn((int16_t)(x + w / 2), (int16_t)(y + h / 2), 6, 26,
                ST7735_RGB565(255, 200, 80), ST7735_RGB565(255, 90, 40));

    g_score += ENEMY_SCORE[g_enemies[i].type];
    if (g_score > g_hiscore) g_hiscore = g_score;
    g_hudDirty = 1;
    g_enemies[i].on = 0;
}

/* 玩家被打中：先扣护盾，护盾没了再扣命 */
static void Player_Hit(void)
{
    if (g_invul) return;

    if (g_shield >= 25)
    {
        g_shield -= 25;
    }
    else
    {
        g_shield = 0;
        if (g_lives) g_lives--;
        if (g_lives == 0)
        {
            g_state = ST_OVER;
            Boom_Add((int16_t)((g_px8 >> 8) + PLAYER_W / 2), (int16_t)(PLAYER_Y + PLAYER_H / 2));
            Parts_Spawn((int16_t)((g_px8 >> 8) + PLAYER_W / 2), PLAYER_Y + 5, 12, 34,
                        ST7735_RGB565(255, 220, 120), ST7735_RGB565(255, 90, 40));
        }
        else
        {
            g_shield = 100;                       /* 复活给满护盾 */
        }
    }

    g_invul = INVUL_FRAMES;
    g_flash = FLASH_FRAMES;
    g_shake = SHAKE_FRAMES;
    g_hudDirty = 1;
    g_full = 1;                                   /* 闪白/震动要整块重画 */
}

static void Enemy_Spawn(void)
{
    uint8_t i, t;
    uint8_t lvl;

    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (g_enemies[i].on) continue;

        /* 分数越高越容易出精英机 */
        lvl = (uint8_t)(Rnd() % 100);
        if (g_score > 300 && lvl < 25)      t = 2;
        else if (g_score > 120 && lvl < 55) t = 1;
        else                                t = 0;

        g_enemies[i].type = t;
        g_enemies[i].hp   = ENEMY_HP[t];
        g_enemies[i].hit  = 0;
        g_enemies[i].x8   = (int32_t)(2 + Rnd() % (SCR_W - 4 - ENEMY_W[t])) << 8;
        g_enemies[i].y8   = (int32_t)(PLAY_Y0 - ENEMY_H[t] - 2) << 8;
        g_enemies[i].on   = 1;
        return;
    }
}

static void Update(uint32_t dtMs)
{
    uint8_t i, j;
    int16_t x, y, w, h;
    int16_t px = (int16_t)(g_px8 >> 8);

    /* ---------- 1. 玩家左右移动（8.8 定点） ---------- */
    {
        int32_t step = (int32_t)PLAYER_SPEED * (int32_t)dtMs * 256 / 1000;
        if (g_holdL && !g_holdR) { g_px8 -= step; g_tilt = 1; }
        else if (g_holdR && !g_holdL) { g_px8 += step; g_tilt = 2; }
        else { g_tilt = 0; }

        if (g_px8 < (int32_t)(2 << 8))                              g_px8 = (int32_t)(2 << 8);
        if (g_px8 > (int32_t)((SCR_W - PLAYER_W - 2) << 8))         g_px8 = (int32_t)((SCR_W - PLAYER_W - 2) << 8);
        if ((g_px8 >> 8) != px) Dirty_Add(px, PLAYER_Y, (int16_t)(px + PLAYER_W - 1), PLAYER_Y + PLAYER_H + 4);
    }
    px = (int16_t)(g_px8 >> 8);
    Dirty_Add(px, PLAYER_Y, (int16_t)(px + PLAYER_W - 1), PLAYER_Y + PLAYER_H + 4);

    /* ---------- 2. 自动开火 ---------- */
    if (GetTick() - g_fireTick >= FIRE_PERIOD_MS)
    {
        g_fireTick = GetTick();
        for (i = 0; i < MAX_BULLETS; i++)
        {
            if (g_bullets[i].on) continue;
            g_bullets[i].on = 1;
            g_bullets[i].x8 = (int32_t)(px + PLAYER_W / 2) << 8;
            g_bullets[i].y8 = (int32_t)(PLAYER_Y - BULLET_P_H) << 8;
            break;
        }
    }

    /* ---------- 3. 我方子弹 ---------- */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (!g_bullets[i].on) continue;
        Dirty_Add((int16_t)(g_bullets[i].x8 >> 8), (int16_t)(g_bullets[i].y8 >> 8),
                  (int16_t)((g_bullets[i].x8 >> 8) + BULLET_P_W - 1),
                  (int16_t)((g_bullets[i].y8 >> 8) + BULLET_P_H - 1));

        g_bullets[i].y8 += (int32_t)BULLET_VY * (int32_t)dtMs * 256 / 1000;

        if ((g_bullets[i].y8 >> 8) < PLAY_Y0 - 8) { g_bullets[i].on = 0; continue; }

        Dirty_Add((int16_t)(g_bullets[i].x8 >> 8), (int16_t)(g_bullets[i].y8 >> 8),
                  (int16_t)((g_bullets[i].x8 >> 8) + BULLET_P_W - 1),
                  (int16_t)((g_bullets[i].y8 >> 8) + BULLET_P_H - 1));
    }

    /* ---------- 3.5 敌机按节奏出场 ---------- */
    if (GetTick() - g_spawnTick >= SPAWN_MS)
    {
        g_spawnTick = GetTick();
        Enemy_Spawn();
    }

    /* ---------- 4. 敌机 ---------- */
    for (i = 0; i < MAX_ENEMIES; i++)
    {
        if (!g_enemies[i].on) continue;

        Enemy_Box(i, &x, &y, &w, &h);
        Dirty_Add(x, y, (int16_t)(x + w - 1), (int16_t)(y + h - 1));

        if (g_enemies[i].hit) g_enemies[i].hit--;

        g_enemies[i].y8 += (int32_t)ENEMY_VY[g_enemies[i].type] * (int32_t)dtMs * 256 / 1000;

        Enemy_Box(i, &x, &y, &w, &h);
        Dirty_Add(x, y, (int16_t)(x + w - 1), (int16_t)(y + h - 1));

        if (y > ST7735_HEIGHT) { g_enemies[i].on = 0; continue; }

        /* 战斗机和精英机会往下打 */
        if (g_enemies[i].type >= 1 && (Rnd() % 220) == 0)
        {
            for (j = 0; j < MAX_EBULLETS; j++)
            {
                if (g_ebullets[j].on) continue;
                g_ebullets[j].on = 1;
                g_ebullets[j].x8 = (int32_t)(x + w / 2) << 8;
                g_ebullets[j].y8 = (int32_t)(y + h) << 8;
                break;
            }
        }

        /* 撞到玩家 */
        if (!(x + w < px + 2 || x > px + PLAYER_W - 2 || y + h < PLAYER_Y + 2 || y > PLAYER_Y + PLAYER_H - 2))
        {
            Enemy_Kill(i);
            Player_Hit();
            continue;
        }
    }

    /* ---------- 5. 敌方子弹 ---------- */
    for (i = 0; i < MAX_EBULLETS; i++)
    {
        if (!g_ebullets[i].on) continue;
        Dirty_Add((int16_t)(g_ebullets[i].x8 >> 8), (int16_t)(g_ebullets[i].y8 >> 8),
                  (int16_t)((g_ebullets[i].x8 >> 8) + BULLET_E_W - 1),
                  (int16_t)((g_ebullets[i].y8 >> 8) + BULLET_E_H - 1));

        g_ebullets[i].y8 += (int32_t)EBULLET_VY * (int32_t)dtMs * 256 / 1000;

        if ((g_ebullets[i].y8 >> 8) > ST7735_HEIGHT) { g_ebullets[i].on = 0; continue; }

        Dirty_Add((int16_t)(g_ebullets[i].x8 >> 8), (int16_t)(g_ebullets[i].y8 >> 8),
                  (int16_t)((g_ebullets[i].x8 >> 8) + BULLET_E_W - 1),
                  (int16_t)((g_ebullets[i].y8 >> 8) + BULLET_E_H - 1));

        if (!( (g_ebullets[i].x8 >> 8) + BULLET_E_W < px + 2 ||
               (g_ebullets[i].x8 >> 8) > px + PLAYER_W - 2 ||
               (g_ebullets[i].y8 >> 8) + BULLET_E_H < PLAYER_Y + 2 ||
               (g_ebullets[i].y8 >> 8) > PLAYER_Y + PLAYER_H - 2))
        {
            g_ebullets[i].on = 0;
            Player_Hit();
        }
    }

    /* ---------- 6. 我方子弹 vs 敌机（AABB） ---------- */
    for (i = 0; i < MAX_BULLETS; i++)
    {
        if (!g_bullets[i].on) continue;

        for (j = 0; j < MAX_ENEMIES; j++)
        {
            if (!g_enemies[j].on) continue;

            Enemy_Box(j, &x, &y, &w, &h);

            if (!( (g_bullets[i].x8 >> 8) + BULLET_P_W < x ||
                   (g_bullets[i].x8 >> 8) > x + w - 1 ||
                   (g_bullets[i].y8 >> 8) + BULLET_P_H < y ||
                   (g_bullets[i].y8 >> 8) > y + h - 1))
            {
                g_bullets[i].on = 0;
                g_enemies[j].hit = 3;
                if (g_enemies[j].hp) g_enemies[j].hp--;
                if (g_enemies[j].hp == 0) Enemy_Kill(j);
                else Parts_Spawn((int16_t)(x + w / 2), (int16_t)(y + h / 2), 2, 12,
                                 ST7735_RGB565(255, 240, 180), ST7735_RGB565(120, 220, 255));
                break;
            }
        }
    }

    /* ---------- 7. 爆炸帧动画 ---------- */
    for (i = 0; i < MAX_BOOMS; i++)
    {
        if (g_booms[i].frame >= BOOM_FRAMES) continue;
        if (++g_booms[i].tick >= 4)
        {
            g_booms[i].tick = 0;
            g_booms[i].frame++;
        }
        Dirty_Add(g_booms[i].x, g_booms[i].y,
                  (int16_t)(g_booms[i].x + BOOM_W - 1), (int16_t)(g_booms[i].y + BOOM_H - 1));
    }

    /* ---------- 8. 粒子 ---------- */
    for (i = 0; i < MAX_PARTS; i++)
    {
        int32_t tick;
        if (g_parts[i].life == 0) continue;

        Dirty_Add((int16_t)(g_parts[i].x - 1), (int16_t)(g_parts[i].y - 1),
                  (int16_t)(g_parts[i].x + 1), (int16_t)(g_parts[i].y + 1));

        tick = (int32_t)dtMs;
        g_parts[i].x += (int16_t)(g_parts[i].vx * tick / 1000);
        g_parts[i].y += (int16_t)(g_parts[i].vy * tick / 1000);
        g_parts[i].vy += 20;                       /* 一点点重力，碎屑会往下飘 */
        g_parts[i].life--;

        /* 颜色随生命衰减：先白黄，再橙，最后暗红 */
        if (g_parts[i].life * 3 < g_parts[i].max)
            g_parts[i].col = ST7735_RGB565(140, 30, 30);
        else if (g_parts[i].life * 3 < g_parts[i].max * 2)
            g_parts[i].col = ST7735_RGB565(255, 120, 40);

        Dirty_Add((int16_t)(g_parts[i].x - 1), (int16_t)(g_parts[i].y - 1),
                  (int16_t)(g_parts[i].x + 1), (int16_t)(g_parts[i].y + 1));
    }

    /* ---------- 9. 星空滚动节拍 ---------- */
    if (GetTick() - g_scrollTick >= SCROLL_TICK_MS)
    {
        g_scrollTick = GetTick();
        g_tickCnt++;
        g_full = 1;                                /* 背景动了，整块重画一次 */
    }

    /* ---------- 10. 计时类状态 ---------- */
    if (g_invul) g_invul--;
    if (g_flash) g_flash--;
    if (g_shake) g_shake--;
    if (g_hudDirty) { Hud_Text(); g_hudDirty = 0; }
}

/* ==========================================================================
 *                        八、开局 / 复位
 * ========================================================================== */

static void New_Game(void)
{
    uint8_t i;

    for (i = 0; i < MAX_BULLETS; i++)  g_bullets[i].on = 0;
    for (i = 0; i < MAX_EBULLETS; i++) g_ebullets[i].on = 0;
    for (i = 0; i < MAX_ENEMIES; i++)  g_enemies[i].on = 0;
    for (i = 0; i < MAX_PARTS; i++)    g_parts[i].life = 0;
    for (i = 0; i < MAX_BOOMS; i++)    g_booms[i].frame = BOOM_FRAMES;

    g_px8    = (int32_t)((SCR_W / 2 - PLAYER_W / 2) << 8);
    g_lives  = 3;
    g_shield = 100;
    g_tilt   = 0;
    g_score  = 0;
    g_invul  = INVUL_FRAMES;
    g_flash  = 0;
    g_shake  = 0;
    g_holdL = g_holdR = 0;

    g_fireTick   = GetTick();
    g_scrollTick = GetTick();
    g_spawnTick  = GetTick();
    g_hudDirty   = 1;
}

static void Stars_Init(void)
{
    uint8_t i;
    for (i = 0; i < MAX_STARS; i++)
    {
        g_stars[i].x     = (int16_t)(Rnd() % SCR_W);
        g_stars[i].base  = (int16_t)(Rnd() % PLAY_H);
        /* 大约 1/2 远景、1/3 中景、其余近景 */
        {
            uint8_t r = (uint8_t)(Rnd() % 100);
            g_stars[i].layer = (r < 50) ? 0 : ((r < 83) ? 1 : 2);
        }
    }
}

/* ==========================================================================
 *                        九、状态机 + 对外接口
 * ========================================================================== */

static void Reset_To_Menu(void)
{
    New_Game();
    g_state = ST_MENU;
}

void Shooter_Enter(void)
{
    g_rng = GetTick() | 1;      /* 用开机时间做随机种子 */
    Stars_Init();
    Reset_To_Menu();

    g_drCount  = 0;
    g_full     = 1;
    g_lastTick = GetTick();
    Dirty_Flush();
}

void Shooter_Left(void)
{
    g_holdL = 1;
}

void Shooter_LeftRelease(void)  { g_holdL = 0; }

void Shooter_Right(void)
{
    g_holdR = 1;
}

void Shooter_RightRelease(void) { g_holdR = 0; }

/* 开始 / 复活键（PA2）：标题界面按了开始，撞光了按了重来 */
void Shooter_Start(void)
{
    if (g_state == ST_MENU || g_state == ST_OVER)
    {
        New_Game();
        g_state = ST_PLAY;
        g_full  = 1;                     /* 让这一帧整块重画，把标题字擦掉 */
        Dirty_Flush();
        g_lastTick = GetTick();
    }
}

void Shooter_Loop(void)
{
    uint32_t now = GetTick();
    uint32_t dt  = now - g_lastTick;

    if (dt < 20) return;                 /* 帧节拍上限约 50 帧/秒 */
    if (dt > 60) dt = 60;                /* 卡顿时限幅，避免一步跨太大 */
    g_lastTick = now;

    g_drCount = 0;
    g_full    = 0;

    switch (g_state)
    {
    case ST_MENU:
        g_full = 1;                                   /* 标题界面整屏画，等开始键 */
        break;

    case ST_PLAY:
        Update(dt);
        break;

    case ST_OVER:
        g_full = 1;                                   /* 停在结束画面，等开始键复活 */
        break;
    }

    Dirty_Flush();
}
