/*
 * camera_debug.h
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */

#ifndef INC_CAMERA_DEBUG_H_
#define INC_CAMERA_DEBUG_H_

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

void Camera_Debug_Init(void);

#endif /* INC_CAMERA_DEBUG_H_ */
