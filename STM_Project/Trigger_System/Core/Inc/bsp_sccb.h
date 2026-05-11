/*
 * bsp_sccb.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_BSP_SCCB_H_
#define INC_BSP_SCCB_H_

#include "main.h"

void BSP_SCCB_Init(void);
uint8_t BSP_SCCB_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);
uint8_t BSP_SCCB_ReadReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data);

#endif /* INC_BSP_SCCB_H_ */
