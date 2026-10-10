/**
  ******************************************************************************
  * @file    font8x8.h
  * @brief   8x8 ASCII 点阵字库（字符 32~126），由 my_app/gen_font8x8.py 生成
  *
  * 每个字符 8 个字节，每字节是一行，bit7 在最左边。
  * 取字：FONT8X8[字符 - 32][行]
  ******************************************************************************
  */
#ifndef _FONT8X8_H_
#define _FONT8X8_H_

#include "stm32f10x.h"

#define FONT8X8_FIRST   32
#define FONT8X8_COUNT   95
#define FONT8X8_W       8
#define FONT8X8_H       8

/* 字符 c(32~126) 的第 r(0~7) 行点阵 */
extern const uint8_t FONT8X8[FONT8X8_COUNT][FONT8X8_H];

#endif
