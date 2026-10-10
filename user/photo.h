/**
  ******************************************************************************
  * @file    photo.h
  * @brief   照片页的数据（128x160，256 色调色板），由 gen_photo.py 生成
  *
  * 调色板 + 下标总占用 = 512 + 20480 = 20992 字节(约 20.5KB)
  ******************************************************************************
  */
#ifndef _PHOTO_H_
#define _PHOTO_H_

#include "stm32f10x.h"

#define PHOTO_WIDTH   128
#define PHOTO_HEIGHT  160
#define PHOTO_COLORS  256

extern const uint16_t PHOTO_PAL[PHOTO_COLORS];
extern const uint8_t  PHOTO_IDX[PHOTO_WIDTH * PHOTO_HEIGHT];

#endif
