/**
  ******************************************************************************
  * @file    mpu6050.c
  * @brief   MPU6050 六轴传感器驱动源文件（本项目只用加速度计）
  *
  *  读写寄存器的方式：MPU6050 是 I2C 从机，寄存器一共 8 位地址。
  *   - 写：START -> 从机地址(写) -> 寄存器地址 -> 数据... -> STOP
  *   - 读：先写一次寄存器地址（把器件内部的“读指针”挪过去），
  *        再 START -> 从机地址(读) -> 读出数据... -> STOP
  *  这套“先写地址再读”的时序就是 Arduino Wire 库对 MPU6050 的标准读法，
  *  本工程直接复用 my_lib/i2c.c 里的两个收发函数拼出来。
  ******************************************************************************
  */

#include "mpu6050.h"
#include "i2c.h"
#include "delay.h"

/* MPU6050 常用寄存器地址 */
#define REG_SMPLRT_DIV      0x19    /* 采样率分频：采样率 = 1kHz / (1 + 值) */
#define REG_CONFIG          0x1A    /* 数字低通滤波 DLPF 配置 */
#define REG_ACCEL_CONFIG    0x1C    /* 加速度量程：0x00=±2g, 0x08=±4g, 0x10=±8g, 0x18=±16g */
#define REG_ACCEL_XOUT_H    0x3B    /* 加速度输出：X高 X低 Y高 Y低 Z高 Z低，共 6 字节 */
#define REG_PWR_MGMT_1      0x6B    /* 电源管理1：bit6=SLEEP，写0x00退出睡眠 */
#define REG_WHO_AM_I        0x75    /* 器件 ID，正常应读到 0x68 */

//
// @简介：写一个寄存器
// @参数：I2Cx - I2C 接口；reg - 寄存器地址；val - 要写入的值
// @返回值：0 - 成功；非0 - 失败
//
static int MPU6050_WriteReg(I2C_TypeDef *I2Cx, uint8_t reg, uint8_t val)
{
    uint8_t buf[2];

    buf[0] = reg;   /* 第 1 个字节：寄存器地址 */
    buf[1] = val;   /* 第 2 个字节：数据 */

    return My_I2C_SendBytes(I2Cx, MPU6050_ADDR, buf, 2);
}

//
// @简介：连续读若干个寄存器
// @参数：I2Cx - I2C 接口；reg - 起始寄存器地址；pBuf - 接收缓冲区；size - 读几个字节
// @返回值：0 - 成功；-1 - 失败
//
// @注意：先“写地址”（只发 1 个字节的寄存器地址）再“读数据”，是 MPU6050 的
//        标准读法。中途发一次 STOP 也不会丢指针，器件会记住上次访问的寄存器。
//
static int MPU6050_ReadRegs(I2C_TypeDef *I2Cx, uint8_t reg, uint8_t *pBuf, uint16_t size)
{
    /* 第 1 步：把器件内部的读指针定位到 reg */
    if (My_I2C_SendBytes(I2Cx, MPU6050_ADDR, &reg, 1) != 0) return -1;

    /* 第 2 步：从该指针开始连续读 size 个字节 */
    if (My_I2C_ReceiveBytes(I2Cx, MPU6050_ADDR, pBuf, size) != 0) return -1;

    return 0;
}

//
// @简介：读 WHO_AM_I 寄存器
// @返回值：0 - 成功；-1 - 失败
//
int MPU6050_ReadID(I2C_TypeDef *I2Cx, uint8_t *pId)
{
    return MPU6050_ReadRegs(I2Cx, REG_WHO_AM_I, pId, 1);
}

//
// @简介：初始化 MPU6050
// @返回值：0 - 成功；-1 - 没找到器件
//
int MPU6050_Init(I2C_TypeDef *I2Cx)
{
    uint8_t id = 0;

    /* 先握个手：能读到 WHO_AM_I 就说明总线通、地址对。
     * 这里不强制 id==0x68，因为有些“MPU6050 模块”其实用的是引脚兼容的
     * MPU6500 等芯片，ID 不是 0x68，但寄存器用法一样。 */
    if (MPU6050_ReadID(I2Cx, &id) != 0) return -1;

    if (MPU6050_WriteReg(I2Cx, REG_PWR_MGMT_1, 0x00) != 0) return -1;  /* 退出睡眠 */
    Delay(30);                                                          /* 等内部时钟稳定 */

    MPU6050_WriteReg(I2Cx, REG_SMPLRT_DIV, 0x04);       /* 采样率 1kHz/(1+4) = 200Hz */
    MPU6050_WriteReg(I2Cx, REG_CONFIG, 0x03);           /* 数字低通滤波 44Hz，抑制手抖 */
    MPU6050_WriteReg(I2Cx, REG_ACCEL_CONFIG, 0x00);     /* 加速度量程 ±2g */

    Delay(10);
    return 0;
}

//
// @简介：读取三轴加速度原始值
// @返回值：0 - 成功；-1 - 读取失败
//
int MPU6050_ReadAccel(I2C_TypeDef *I2Cx, MPU6050_Accel *accel)
{
    uint8_t buf[6];

    /* 从 ACCEL_XOUT_H 开始连续读 6 个字节：X、Y、Z 各 2 字节（高字节在前） */
    if (MPU6050_ReadRegs(I2Cx, REG_ACCEL_XOUT_H, buf, 6) != 0) return -1;

    /* 两个字节拼成一个 16 位有符号数 */
    accel->ax = (int16_t)((buf[0] << 8) | buf[1]);
    accel->ay = (int16_t)((buf[2] << 8) | buf[3]);
    accel->az = (int16_t)((buf[4] << 8) | buf[5]);

    return 0;
}
