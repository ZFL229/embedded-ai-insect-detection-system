/*
 * bsp_sccb.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_BSP_SCCB_H_
#define INC_BSP_SCCB_H_

#include "main.h"

/* 初始化软件 SCCB 总线空闲态：SDA/SCL 均释放为高电平。 */
void BSP_SCCB_Init(void);

/* 向 OV2640 等 SCCB 设备写入一个 8 位寄存器。 */
uint8_t BSP_SCCB_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);

/* 从 OV2640 等 SCCB 设备读取一个 8 位寄存器。 */
uint8_t BSP_SCCB_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data);

#endif /* INC_BSP_SCCB_H_ */
