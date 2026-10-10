/**
  ******************************************************************************
  * @file    game.h
  * @brief   太空射击（第 4 页）对外接口
  *
  *  竖屏 128x160。两个游戏键当"左 / 右"，开始/复活用双击：
  *      PB0 = 左移      PB1 = 右移
  *      双击 PB0 或 PB1 = 开始 / 复活（只在标题界面和结束界面有效）
  *  PA2/PA3 仍然是翻页键（靠它们翻走离开这一页）。
  ******************************************************************************
  */
#ifndef _GAME_H_
#define _GAME_H_

#include "stm32f10x.h"

/* @简介：切到太空射击页时调用（复位到标题界面并画一帧） */
void Shooter_Enter(void);

/* @简介：主循环里每帧调用一次（状态机 + 更新 + 局部刷新） */
void Shooter_Loop(void);

/* @简介：左 / 右键按下时调用 */
void Shooter_Left(void);
void Shooter_Right(void);

/* @简介：左 / 右键松开时调用 */
void Shooter_LeftRelease(void);
void Shooter_RightRelease(void);

/* @简介：开始 / 复活键（标题界面开始，撞光了重来） */
void Shooter_Start(void);

#endif
