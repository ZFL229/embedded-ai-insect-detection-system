/*
 * bsp_pcf8574.c
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#include "main.h"
#include "bsp_pcf8574.h"

extern I2C_HandleTypeDef hi2c2;

#define PCF8574_ADDR   (0x20 << 1)
#define CAMERA_PWDN_BIT    2

/* 检查 PCF8574 是否在 I2C2 上应答，用于确认扩展 IO 可用。 */
uint8_t BSP_PCF8574_Check(void)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_IsDeviceReady(&hi2c2, PCF8574_ADDR, 3, 100);

    return (status == HAL_OK);
}

/* 向 PCF8574 写入 8 位输出值，调用者负责维护各位含义。 */
HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t value)
{
    return HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &value, 1, 100);
}

/* 拉低摄像头 PWDN 位，使 OV2640 退出掉电状态并开始工作。 */
void BSP_PCF8574_CameraPowerOn(void)
{
    uint8_t value = 0xFF;

    value &= ~(1 << CAMERA_PWDN_BIT);

    BSP_PCF8574_Write(value);
}

/* 拉高摄像头 PWDN 位，使 OV2640 进入掉电状态。 */
void BSP_PCF8574_CameraPowerDown(void)
{
    uint8_t value = 0xFF;

    value |= (1 << CAMERA_PWDN_BIT);

    BSP_PCF8574_Write(value);
}
