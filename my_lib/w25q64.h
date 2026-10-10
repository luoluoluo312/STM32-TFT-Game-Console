/**
  ******************************************************************************
  * @file    w25q64.h
  * @brief   W25Q64 SPI Flash 驱动（8MB = 64Mbit）
  *
  *  和 1.8 寸 TFT 共用同一条 SPI1 总线：
  *       SCK -> PA5, MOSI -> PA7 （和 TFT 共用）
  *       MISO-> PA6             （只有 Flash 用，TFT 是只写屏）
  *       CS  -> PA8             （Flash 自己的片选，和 TFT 的 PA4 分开）
  *  同一时刻只有一个芯片被选中，所以两条 CS 不会打架。
  *
  *  这里只实现"够用"的几个操作：读 ID、读数据、按页写、擦 4KB 扇区。
  ******************************************************************************
  */
#ifndef _W25Q64_H_
#define _W25Q64_H_

#include "stm32f10x.h"

/* 片选和 MISO 的引脚（SCK/MOSI 由 st7735 初始化，这里只补充这两根） */
#define W25Q64_CS_GPIO      GPIOA
#define W25Q64_CS_PIN       GPIO_Pin_8
#define W25Q64_MISO_PIN     GPIO_Pin_6

#define W25Q64_PAGE_SIZE    256             /* 一页 256 字节（写的最小单位） */
#define W25Q64_SECTOR_SIZE  4096            /* 一个扇区 4KB（擦除的最小单位） */

// @简介：初始化（配置 CS / MISO，读一次 JEDEC ID 确认芯片在）
// @返回值：0 - 成功；1 - 没检测到（ID 不对，可能是没接线）
// @注意：必须先调用 ST7735_Init()，因为 SPI1 是它初始化的
uint8_t W25Q64_Init(void);

// @简介：读 JEDEC ID（W25Q64 正常返回 0xEF4017）
uint32_t W25Q64_JedecID(void);

// @简介：从 addr 开始读 len 个字节
void W25Q64_Read(uint32_t Addr, uint8_t *pBuf, uint32_t Len);

// @简介：擦除一个 4KB 扇区
// @参数：Addr - 扇区内任意地址（会自动对齐到扇区首）
void W25Q64_EraseSector(uint32_t Addr);

// @简介：擦除 addr 起始、长度 Len 覆盖到的所有扇区
void W25Q64_EraseRange(uint32_t Addr, uint32_t Len);

// @简介：向 addr 写 Len 个字节（内部自动按 256 字节分页 + 等待写完成）
// @注意：写之前对应区域必须已经擦过（Flash 只能把 1 写成 0）
void W25Q64_Write(uint32_t Addr, const uint8_t *pBuf, uint32_t Len);

#endif
