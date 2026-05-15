/*
 * camera_debug.c
 *
 *  Created on: Apr 29, 2026
 *      Author: linzh
 */
#include "main.h"
#include "camera_debug.h"
#include "camera_capture.h"
#include <stdarg.h>
#include <stdio.h>

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

#if CAMERA_DEBUG_ENABLE
extern UART_HandleTypeDef huart1;

/* 通过 UART1 输出格式化调试信息，受 CAMERA_DEBUG_ENABLE 控制。 */
void CameraDebug_Printf(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0)
    {
        return;
    }

    if (len >= (int)sizeof(buf))
    {
        len = sizeof(buf) - 1;
    }

    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 100U);
}

/* 输出一次采集后的 JPEG marker、有效长度和 DCMI/DMA 状态。 */
void CameraDebug_LogCapture(DCMI_HandleTypeDef *hdcmi)
{
    static uint32_t cap_count = 0U;
    uint8_t *buf = (uint8_t *)cam_frame_buffer;
    int32_t soi = -1;
    int32_t eoi = -1;
    uint8_t valid = (cam_state.jpg_len > 0U) ? 1U : 0U;
    HAL_DCMI_StateTypeDef dcmi_state = HAL_DCMI_STATE_RESET;
    HAL_DMA_StateTypeDef dma_state = HAL_DMA_STATE_RESET;

    cap_count++;

    for (uint32_t i = 0; i < CAM_FRAME_BUFFER_SIZE - 1U; i++)
    {
        if (buf[i] == 0xFFU && buf[i + 1U] == 0xD8U && soi < 0)
        {
            soi = (int32_t)i;
        }

        if (buf[i] == 0xFFU && buf[i + 1U] == 0xD9U)
        {
            eoi = (int32_t)i;
        }
    }

    if (hdcmi != NULL)
    {
        dcmi_state = HAL_DCMI_GetState(hdcmi);

        if (hdcmi->DMA_Handle != NULL)
        {
            dma_state = HAL_DMA_GetState(hdcmi->DMA_Handle);
        }
    }

    CAM_DEBUG(
        "[CAP] N=%lu SOI=%ld EOI=%ld LEN=%lu VALID=%u DCMI=%d DMA=%d\r\n",
        (unsigned long)cap_count,
        (long)soi,
        (long)eoi,
        (unsigned long)cam_state.jpg_len,
        valid,
        (int)dcmi_state,
        (int)dma_state
    );
}
#endif

/* 复位所有调试观测变量，便于重新开始一轮摄像头初始化/采集实验。 */
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
