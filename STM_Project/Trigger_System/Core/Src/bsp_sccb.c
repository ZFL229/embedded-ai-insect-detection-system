/*
 * bsp_sccb.c
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#include "bsp_sccb.h"

#define SCCB_DELAY()  for(volatile int i = 0; i < 80; i++)

#define SCCB_SDA_HIGH() HAL_GPIO_WritePin(DCMI_SDA_GPIO_Port, DCMI_SDA_Pin, GPIO_PIN_SET)
#define SCCB_SDA_LOW()  HAL_GPIO_WritePin(DCMI_SDA_GPIO_Port, DCMI_SDA_Pin, GPIO_PIN_RESET)

#define SCCB_SCL_HIGH() HAL_GPIO_WritePin(DCMI_SCL_GPIO_Port, DCMI_SCL_Pin, GPIO_PIN_SET)
#define SCCB_SCL_LOW()  HAL_GPIO_WritePin(DCMI_SCL_GPIO_Port, DCMI_SCL_Pin, GPIO_PIN_RESET)

#define SCCB_SDA_READ() HAL_GPIO_ReadPin(DCMI_SDA_GPIO_Port, DCMI_SDA_Pin)

/* 将 SDA 配置为开漏输出，用于主机驱动 SCCB 数据线。 */
static void SCCB_SDA_Out(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DCMI_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DCMI_SDA_GPIO_Port, &GPIO_InitStruct);
}

/* 将 SDA 配置为输入，用于读取 ACK 或寄存器数据。 */
static void SCCB_SDA_In(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DCMI_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DCMI_SDA_GPIO_Port, &GPIO_InitStruct);
}

/* 初始化软件 SCCB 总线，并释放 SDA/SCL 到空闲高电平。 */
void BSP_SCCB_Init(void)
{
    SCCB_SDA_Out();

    SCCB_SDA_HIGH();
    SCCB_SCL_HIGH();
    SCCB_DELAY();
}

/* 产生 SCCB 起始条件：SCL 为高时 SDA 由高拉低。 */
static void SCCB_Start(void)
{
    SCCB_SDA_Out();

    SCCB_SDA_HIGH();
    SCCB_SCL_HIGH();
    SCCB_DELAY();

    SCCB_SDA_LOW();
    SCCB_DELAY();

    SCCB_SCL_LOW();
    SCCB_DELAY();
}

/* 产生 SCCB 停止条件：SCL 为高时 SDA 由低释放为高。 */
static void SCCB_Stop(void)
{
    SCCB_SDA_Out();

    SCCB_SCL_LOW();
    SCCB_SDA_LOW();
    SCCB_DELAY();

    SCCB_SCL_HIGH();
    SCCB_DELAY();

    SCCB_SDA_HIGH();
    SCCB_DELAY();
}

/* 按 MSB first 写出 1 字节，并读取从设备 ACK。返回 0 表示 ACK。 */
static uint8_t SCCB_WriteByte(uint8_t data)
{
    uint8_t i;
    uint8_t ack;

    SCCB_SDA_Out();

    for (i = 0; i < 8; i++)
    {
        SCCB_SCL_LOW();

        if (data & 0x80)
            SCCB_SDA_HIGH();
        else
            SCCB_SDA_LOW();

        data <<= 1;

        SCCB_DELAY();

        SCCB_SCL_HIGH();
        SCCB_DELAY();
    }

    SCCB_SCL_LOW();
    SCCB_SDA_In();
    SCCB_DELAY();

    SCCB_SCL_HIGH();
    SCCB_DELAY();

    ack = SCCB_SDA_READ();

    SCCB_SCL_LOW();
    SCCB_SDA_Out();

    return ack;
}

/* 按 MSB first 从 SDA 读取 1 字节。 */
static uint8_t SCCB_ReadByte(void)
{
    uint8_t i;
    uint8_t data = 0;

    SCCB_SDA_In();

    for (i = 0; i < 8; i++)
    {
        data <<= 1;

        SCCB_SCL_LOW();
        SCCB_DELAY();

        SCCB_SCL_HIGH();
        SCCB_DELAY();

        if (SCCB_SDA_READ())
            data |= 0x01;
    }

    SCCB_SCL_LOW();
    SCCB_SDA_Out();

    return data;
}

/* 读寄存器最后一个字节后发送 NACK，结束本次读取。 */
static void SCCB_NoAck(void)
{
    SCCB_SDA_Out();

    SCCB_SDA_HIGH();
    SCCB_DELAY();

    SCCB_SCL_HIGH();
    SCCB_DELAY();

    SCCB_SCL_LOW();
    SCCB_DELAY();
}

/* 写 OV2640 等 SCCB 设备的单个寄存器。返回 1 表示成功。 */
uint8_t BSP_SCCB_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    SCCB_Start();

    if (SCCB_WriteByte(dev_addr)) goto error;
    if (SCCB_WriteByte(reg_addr)) goto error;
    if (SCCB_WriteByte(data)) goto error;

    SCCB_Stop();
    return 1;

error:
    SCCB_Stop();
    return 0;
}

/* 读取 OV2640 等 SCCB 设备的单个寄存器。返回 1 表示成功。 */
uint8_t BSP_SCCB_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    SCCB_Start();

    if (SCCB_WriteByte(dev_addr)) goto error;
    if (SCCB_WriteByte(reg_addr)) goto error;

    SCCB_Stop();
    SCCB_DELAY();

    SCCB_Start();

    if (SCCB_WriteByte(dev_addr | 0x01)) goto error;

    *data = SCCB_ReadByte();

    SCCB_NoAck();
    SCCB_Stop();

    return 1;

error:
    SCCB_Stop();
    return 0;
}
