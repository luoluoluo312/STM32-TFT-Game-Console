/**
  ******************************************************************************
  * @file    sprites.h
  * @brief   太空射击用的像素精灵，由 my_app/gen_sprites.py 生成
  *
  * 每像素 1 字节，是 SPRITE_PALETTE 的下标；下标 0 表示透明，不画。
  * 比例：飞船 11x11，侦察机 9x9，战斗机 11x11，精英机 13x13，爆炸 17x17
  ******************************************************************************
  */
#ifndef _SPRITES_H_
#define _SPRITES_H_

#include "stm32f10x.h"

#define SPRITE_PALETTE_COUNT 16
extern const uint16_t SPRITE_PALETTE[SPRITE_PALETTE_COUNT];

#define PLAYER_W 11
#define PLAYER_H 11
extern const uint8_t SPR_PLAYER[3][PLAYER_W * PLAYER_H];

#define SPR_SCOUT_W 9
#define SPR_SCOUT_H 9
extern const uint8_t SPR_SCOUT[SPR_SCOUT_W * SPR_SCOUT_H];

#define SPR_FIGHTER_W 11
#define SPR_FIGHTER_H 11
extern const uint8_t SPR_FIGHTER[SPR_FIGHTER_W * SPR_FIGHTER_H];

#define SPR_ELITE_W 13
#define SPR_ELITE_H 13
extern const uint8_t SPR_ELITE[SPR_ELITE_W * SPR_ELITE_H];

#define BULLET_P_W 3
#define BULLET_P_H 7
extern const uint8_t SPR_BULLET_P[BULLET_P_W * BULLET_P_H];

#define BULLET_E_W 3
#define BULLET_E_H 5
extern const uint8_t SPR_BULLET_E[BULLET_E_W * BULLET_E_H];

#define BOOM_W 17
#define BOOM_H 17
#define BOOM_FRAMES 6
extern const uint8_t SPR_BOOM[BOOM_FRAMES][BOOM_W * BOOM_H];

#endif
