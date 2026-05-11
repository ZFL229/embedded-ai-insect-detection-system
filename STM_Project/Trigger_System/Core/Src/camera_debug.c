/*
 * camera_debug.c
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */
#include "main.h"
#include "camera_debug.h"

volatile uint8_t dbg_step = 0;
volatile uint8_t dbg_pcf_ok = 0;
volatile HAL_StatusTypeDef dbg_hal_status = HAL_OK;
volatile uint32_t dbg_hal_error = 0;
volatile uint8_t dbg_pcf_write_ok = 0;
volatile uint8_t dbg_ov_pid = 0;
volatile uint8_t dbg_ov_ver = 0;
volatile uint8_t dbg_ov_mid_h = 0;
volatile uint8_t dbg_ov_mid_l = 0;
volatile uint8_t dbg_ov_id_ok = 0;
volatile uint8_t dbg_vsync = 0;
volatile uint8_t dbg_href = 0;
volatile uint8_t dbg_pclk = 0;
volatile uint32_t dbg_vsync_cnt = 0;
volatile uint32_t dbg_href_cnt = 0;
volatile uint32_t dbg_pclk_cnt = 0;

volatile uint8_t dbg_ov_min_init_ok = 0;

volatile uint8_t dbg_ov_basic_init_ok = 0;

void Camera_Debug_Init(void)
{
    dbg_step = 0;
    dbg_pcf_ok = 0;
    dbg_pcf_write_ok = 0;
    dbg_hal_status = HAL_OK;
    dbg_hal_error = 0;
    dbg_ov_pid = 0;
    dbg_ov_ver = 0;
    dbg_ov_mid_h = 0;
    dbg_ov_mid_l = 0;
    dbg_ov_id_ok = 0;

    dbg_vsync = 0;
    dbg_href = 0;
    dbg_pclk = 0;

    dbg_vsync_cnt = 0;
    dbg_href_cnt = 0;
    dbg_pclk_cnt = 0;

    dbg_ov_min_init_ok = 0;

    dbg_ov_basic_init_ok = 0;
}
