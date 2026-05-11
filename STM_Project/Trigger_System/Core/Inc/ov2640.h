/*
 * ov2640.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_OV2640_H_
#define INC_OV2640_H_

#include "main.h"

#define OV2640_SCCB_ADDR   0x60

uint8_t OV2640_ReadID(uint8_t *mid_h, uint8_t *mid_l, uint8_t *pid, uint8_t *ver);
void OV2640_HardwareReset(void);

uint8_t OV2640_WriteReg(uint8_t reg, uint8_t data);
uint8_t OV2640_Init_320x240_JPEG(void);
uint8_t OV2640_Init_640x480_JPEG(void);
uint8_t OV2640_Init_800x600_JPEG(void);
uint8_t OV2640_Init_1024x768_JPEG(void);
uint8_t OV2640_Init_1280x960_JPEG(void);
uint8_t OV2640_SetLightMode(uint8_t mode);

#endif /* INC_OV2640_H_ */
