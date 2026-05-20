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

/* 读取 OV2640 厂商 ID 和产品 ID，用于上电后连通性检查。 */
uint8_t OV2640_ReadID(uint8_t *mid_h, uint8_t *mid_l, uint8_t *pid, uint8_t *ver);

/* 控制 OV2640 RESET 引脚，执行一次硬件复位。 */
void OV2640_HardwareReset(void);

/* 通过 SCCB 写入单个 OV2640 寄存器。 */
uint8_t OV2640_WriteReg(uint8_t reg, uint8_t data);

/* 按指定分辨率配置 OV2640 JPEG 输出路径。 */
uint8_t OV2640_Init_320x240_JPEG(void);
uint8_t OV2640_Init_640x480_JPEG(void);
uint8_t OV2640_Init_800x600_JPEG(void);
uint8_t OV2640_Init_1024x768_JPEG(void);
uint8_t OV2640_Init_1280x960_JPEG(void);

/* 通过 SCCB 触发软件复位，作为异常恢复的一级手段。 */
uint8_t OV2640_SoftwareReset(void);

/* 进入高级自动白平衡模式，用于 AWB 对比实验。 */
uint8_t OV2640_AdvancedWhiteBalance(void);

/* 进入简单自动白平衡模式，当前稳定基线使用该模式。 */
uint8_t OV2640_SimpleWhiteBalance(void);

/* 设置白平衡/光照模式：0=自动，1=晴天，2=阴天，3=办公室，4=家庭，5=高级 AWB，6=简单 AWB。 */
uint8_t OV2640_SetLightMode(uint8_t mode);

#endif /* INC_OV2640_H_ */
