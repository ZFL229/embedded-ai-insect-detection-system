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

uint8_t BSP_PCF8574_Check(void)
{
    HAL_StatusTypeDef status;

    status = HAL_I2C_IsDeviceReady(&hi2c2, PCF8574_ADDR, 3, 100);

    return (status == HAL_OK);
}

HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t value)
{
    return HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &value, 1, 100);
}

void BSP_PCF8574_CameraPowerOn(void)
{
    uint8_t value = 0xFF;

    value &= ~(1 << CAMERA_PWDN_BIT);

    BSP_PCF8574_Write(value);
}

void BSP_PCF8574_CameraPowerDown(void)
{
    uint8_t value = 0xFF;

    value |= (1 << CAMERA_PWDN_BIT);

    BSP_PCF8574_Write(value);
}
