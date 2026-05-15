/*
 * camera_debug.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_CAMERA_DEBUG_H_
#define INC_CAMERA_DEBUG_H_

#include "main.h"

#define CAMERA_DEBUG_ENABLE 0

/*
 * 摄像头调试输出开关。
 * 关闭时所有 CAM_DEBUG 调用会在预处理阶段变为空操作，不影响正式传输路径。
 */
#if CAMERA_DEBUG_ENABLE
void CameraDebug_Printf(const char *fmt, ...);
void CameraDebug_LogCapture(DCMI_HandleTypeDef *hdcmi);
#define CAM_DEBUG(fmt, ...) CameraDebug_Printf((fmt), ##__VA_ARGS__)
#else
#define CameraDebug_Printf(...)
#define CameraDebug_LogCapture(...)
#define CAM_DEBUG(fmt, ...)
#endif

/* 以下变量用于调试器观察摄像头初始化、ID 读取和同步信号状态。 */
extern volatile uint8_t dbg_step;
extern volatile uint8_t dbg_pcf_ok;
extern volatile HAL_StatusTypeDef dbg_hal_status;
extern volatile uint32_t dbg_hal_error;
extern volatile uint8_t dbg_pcf_write_ok;
extern volatile uint8_t dbg_ov_pid;
extern volatile uint8_t dbg_ov_ver;
extern volatile uint8_t dbg_ov_mid_h;
extern volatile uint8_t dbg_ov_mid_l;
extern volatile uint8_t dbg_ov_id_ok;

extern volatile uint8_t dbg_vsync;
extern volatile uint8_t dbg_href;
extern volatile uint8_t dbg_pclk;

extern volatile uint32_t dbg_vsync_cnt;
extern volatile uint32_t dbg_href_cnt;
extern volatile uint32_t dbg_pclk_cnt;

extern volatile uint8_t dbg_ov_min_init_ok;
extern volatile uint8_t dbg_ov_basic_init_ok;

/* 复位所有摄像头调试观测变量。 */
void Camera_Debug_Init(void);

#endif /* INC_CAMERA_DEBUG_H_ */
