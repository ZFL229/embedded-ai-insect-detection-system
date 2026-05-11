/*
 * bsp_pcf8574.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_BSP_PCF8574_H_
#define INC_BSP_PCF8574_H_

uint8_t BSP_PCF8574_Check(void);
HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t value);

//PWDN控制
void BSP_PCF8574_CameraPowerOn(void);
void BSP_PCF8574_CameraPowerDown(void);

#endif /* INC_BSP_PCF8574_H_ */
