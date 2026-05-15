/*
 * bsp_pcf8574.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_BSP_PCF8574_H_
#define INC_BSP_PCF8574_H_

/* 检查 I2C2 总线上的 PCF8574 扩展 IO 是否应答。 */
uint8_t BSP_PCF8574_Check(void);

/* 向 PCF8574 写入完整 8 位输出值。 */
HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t value);

//PWDN控制
/* 通过 PCF8574 的 PWDN 控制位释放摄像头，使 OV2640 进入工作状态。 */
void BSP_PCF8574_CameraPowerOn(void);

/* 通过 PCF8574 的 PWDN 控制位关闭摄像头输出，用于采集链路重建实验。 */
void BSP_PCF8574_CameraPowerDown(void);

#endif /* INC_BSP_PCF8574_H_ */
