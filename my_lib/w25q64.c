/**
  ******************************************************************************
  * @file    w25q64.c
  * @brief   W25Q64 SPI Flash 驱动源文件
  *
  *  SPI1 由 st7735.c 初始化好了（主机、8 位、模式0），这里直接借用；
  *  本文件只补两件事：把 PA8 配成片选、把 PA6 配成 MISO 输入。
  *
  *  一次完整操作都是：CS 拉低 -> 发命令 -> 发地址 -> 收发数据 -> CS 拉高。
  *
  *  写入的规则（Flash 的物理特性）：
  *    1) 只能把 bit 从 1 写成 0，所以写之前必须先擦（擦完整片变 0xFF）；
  *    2) 一次最多写 256 字节（一页），且不能跨页；
  *    3) 写/擦都要先发 06h（Write Enable），然后轮询状态寄存器等它忙完。
  ******************************************************************************
  */

#include "w25q64.h"
#include "st7735.h"     /* 借用已经初始化好的 SPI1 */
#include "delay.h"

#define CMD_JEDEC_ID     0x9F
#define CMD_READ_DATA    0x03
#define CMD_WRITE_EN     0x06
#define CMD_PAGE_PROG    0x02
#define CMD_SECTOR_ERASE 0x20
#define CMD_READ_SR1     0x05

#define CS_LOW()   GPIO_ResetBits(W25Q64_CS_GPIO, W25Q64_CS_PIN)
#define CS_HIGH()  GPIO_SetBits(W25Q64_CS_GPIO, W25Q64_CS_PIN)

//
// @简介：收发一个字节（主机发 out，同时把从机发回来的读进来）
//
static uint8_t SPI_RW(uint8_t Out)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, Out);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return (uint8_t)SPI_I2S_ReceiveData(SPI1);
}

//
// @简介：发一个字节（不关心读回来的东西）
//
static void SPI_W(uint8_t Out) { (void)SPI_RW(Out); }

//
// @简介：读状态寄存器1的 bit0（BUSY）：1 = 芯片还在写/擦，不能下命令
//
static void W25Q64_WaitBusy(void)
{
    do
    {
        CS_LOW();
        SPI_W(CMD_READ_SR1);
        if ((SPI_RW(0xFF) & 0x01) == 0) { CS_HIGH(); break; }
        CS_HIGH();
    } while (1);
}

//
// @简介：写使能（每次写/擦之前都要发一次）
//
static void W25Q64_WriteEnable(void)
{
    CS_LOW();
    SPI_W(CMD_WRITE_EN);
    CS_HIGH();
}

uint32_t W25Q64_JedecID(void)
{
    uint32_t id;

    CS_LOW();
    SPI_W(CMD_JEDEC_ID);
    id  = (uint32_t)SPI_RW(0xFF) << 16;   /* 厂商 ID，W25Q64 = 0xEF */
    id |= (uint32_t)SPI_RW(0xFF) << 8;    /* 存储类型 */
    id |= (uint32_t)SPI_RW(0xFF);         /* 容量：0x17 = 64Mbit */
    CS_HIGH();

    return id;
}

uint8_t W25Q64_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA8：片选，推挽输出，平时拉高（不选中） */
    gpio.GPIO_Pin   = W25Q64_CS_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(W25Q64_CS_GPIO, &gpio);
    CS_HIGH();

    /* PA6：MISO，主机模式下要配成输入，读 Flash 的数据就从这根线进来 */
    gpio.GPIO_Pin  = W25Q64_MISO_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(W25Q64_CS_GPIO, &gpio);

    Delay(5);

    return (W25Q64_JedecID() == 0xEF4017UL) ? 0 : 1;
}

void W25Q64_Read(uint32_t Addr, uint8_t *pBuf, uint32_t Len)
{
    uint32_t i;

    CS_LOW();
    SPI_W(CMD_READ_DATA);
    SPI_W((uint8_t)(Addr >> 16));
    SPI_W((uint8_t)(Addr >> 8));
    SPI_W((uint8_t)Addr);
    for (i = 0; i < Len; i++) pBuf[i] = SPI_RW(0xFF);
    CS_HIGH();
}

void W25Q64_EraseSector(uint32_t Addr)
{
    W25Q64_WaitBusy();
    W25Q64_WriteEnable();

    CS_LOW();
    SPI_W(CMD_SECTOR_ERASE);
    SPI_W((uint8_t)(Addr >> 16));
    SPI_W((uint8_t)(Addr >> 8));
    SPI_W((uint8_t)Addr);
    CS_HIGH();

    W25Q64_WaitBusy();
}

void W25Q64_EraseRange(uint32_t Addr, uint32_t Len)
{
    uint32_t end = Addr + Len;

    Addr &= ~(uint32_t)(W25Q64_SECTOR_SIZE - 1);      /* 对齐到扇区首 */
    while (Addr < end)
    {
        W25Q64_EraseSector(Addr);
        Addr += W25Q64_SECTOR_SIZE;
    }
}

void W25Q64_Write(uint32_t Addr, const uint8_t *pBuf, uint32_t Len)
{
    uint32_t i, n;

    while (Len)
    {
        /* 一页最多写 256 字节，而且不能跨页 */
        n = W25Q64_PAGE_SIZE - (Addr & (W25Q64_PAGE_SIZE - 1));
        if (n > Len) n = Len;

        W25Q64_WaitBusy();
        W25Q64_WriteEnable();

        CS_LOW();
        SPI_W(CMD_PAGE_PROG);
        SPI_W((uint8_t)(Addr >> 16));
        SPI_W((uint8_t)(Addr >> 8));
        SPI_W((uint8_t)Addr);
        for (i = 0; i < n; i++) SPI_W(pBuf[i]);
        CS_HIGH();

        W25Q64_WaitBusy();

        Addr += n;
        pBuf += n;
        Len  -= n;
    }
}
