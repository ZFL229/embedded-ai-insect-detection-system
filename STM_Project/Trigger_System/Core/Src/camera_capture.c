/*
 * camera_capture.c
 *
 *  Created on: Apr 30, 2026
 *      Author: linzh
 */

#include "camera_capture.h"
#include <string.h>

__attribute__((aligned(32)))
uint32_t cam_frame_buffer[CAM_FRAME_WORDS];

/* 全局采集状态，供 DCMI 回调、采集函数和触发/发送模块共同访问。 */
CameraCapture_StateTypeDef cam_state =
{
    .done = 0,
    .error = 0,
    .start_status = HAL_OK,
    .soi_index = 0xFFFFFFFF,
    .eoi_index = 0xFFFFFFFF
};

/* 清空图像缓冲区，并将采集状态恢复到一次新采集开始前的状态。 */
static void CameraCapture_ResetState(void)
{
    memset(cam_frame_buffer, 0, sizeof(cam_frame_buffer));

    cam_state.done = 0;
    cam_state.error = 0;
    cam_state.soi_index = 0xFFFFFFFF;
    cam_state.eoi_index = 0xFFFFFFFF;
    cam_state.jpg_len = 0;
    cam_state.done_before_delay = 0;
    cam_state.error_before_delay = 0;
    cam_state.done_after_delay = 0;
    cam_state.error_after_delay = 0;
    cam_state.done_after_stop = 0;
    cam_state.error_after_stop = 0;
    cam_state.capture_mode = CAMERA_CAPTURE_MODE_DELAY;
    cam_state.wait_result = 0;
    cam_state.timeout = 0;
}

void CameraCapture_ClearBuffer(void)
{
    CameraCapture_ResetState();
}

/*
 * 启动基础 Snapshot DMA 采集。
 * 该接口保留给早期调试路径使用，实际 A/B 实验路径见下面两个 Snapshot 函数。
 */
HAL_StatusTypeDef CameraCapture_StartSnapshot(DCMI_HandleTypeDef *hdcmi)
{
    CameraCapture_ResetState();

    cam_state.start_status = HAL_DCMI_Start_DMA(
        hdcmi,
        DCMI_MODE_SNAPSHOT,
        (uint32_t)cam_frame_buffer,
        CAM_FRAME_WORDS
    );

    return cam_state.start_status;
}

/* 等待 DCMI 回调置位 done 或 error，超时则返回失败。 */
uint8_t CameraCapture_WaitUntilDone(uint32_t timeout_ms)
{
    uint32_t tick_start = HAL_GetTick();

    while (cam_state.done == 0 && cam_state.error == 0)
    {
        if ((HAL_GetTick() - tick_start) > timeout_ms)
        {
            cam_state.error = 1;
            return 0;
        }
    }

    return cam_state.done;
}

/* 在完整采集缓冲区中扫描 JPEG SOI/EOI，并计算有效 JPEG 长度。 */
void CameraCapture_FindJPEGMarkers(void)
{
    uint8_t *buf = (uint8_t *)cam_frame_buffer;

    cam_state.soi_index = 0xFFFFFFFF;
    cam_state.eoi_index = 0xFFFFFFFF;
    cam_state.jpg_len = 0;

    for (uint32_t i = 0; i < CAM_FRAME_BUFFER_SIZE - 1; i++)
    {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && cam_state.soi_index == 0xFFFFFFFF)
        {
            cam_state.soi_index = i;
        }

        if (buf[i] == 0xFF && buf[i + 1] == 0xD9)
        {
            cam_state.eoi_index = i;
        }
    }

    if (cam_state.soi_index != 0xFFFFFFFF &&
        cam_state.eoi_index != 0xFFFFFFFF &&
        cam_state.eoi_index > cam_state.soi_index)
    {
        cam_state.jpg_len = cam_state.eoi_index - cam_state.soi_index + 2;
    }
}

/* DCMI 捕获到一帧后由 HAL 调用，事件采集模式依赖该标志结束等待。 */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
	(void)hdcmi;
	cam_state.done = 1;
}

/* DCMI 发生错误时由 HAL 调用，采集等待循环据此提前退出。 */
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
	cam_state.error = 1;
}

/*
 * A 组对照路径：启动 Snapshot 后等待固定时间窗，再停止 DCMI 并扫描 JPEG。
 * 该路径用于和事件触发截取路径对比，保留 HAL_Delay(800)。
 */
HAL_StatusTypeDef CameraCapture_SnapshotByDelay(DCMI_HandleTypeDef *hdcmi)
{
    CameraCapture_ResetState();
    cam_state.capture_mode = CAMERA_CAPTURE_MODE_DELAY;

    cam_state.done_before_delay = cam_state.done;
    cam_state.error_before_delay = cam_state.error;

    cam_state.start_status = HAL_DCMI_Start_DMA(
        hdcmi,
        DCMI_MODE_SNAPSHOT,
        (uint32_t)cam_frame_buffer,
        CAM_FRAME_WORDS
    );

    HAL_Delay(800);

    cam_state.done_after_delay = cam_state.done;
    cam_state.error_after_delay = cam_state.error;

    HAL_DCMI_Suspend(hdcmi);
    HAL_DCMI_Stop(hdcmi);

    cam_state.done_after_stop = cam_state.done;
    cam_state.error_after_stop = cam_state.error;

    CameraCapture_FindJPEGMarkers();

    return cam_state.start_status;
}

/*
 * B 组实验路径：启动 Snapshot 后等待 FrameEvent。
 * 只有在 frame_done=1、error=0、timeout=0 时才停止后扫描 JPEG。
 */
HAL_StatusTypeDef CameraCapture_SnapshotByFrameEvent(DCMI_HandleTypeDef *hdcmi, uint32_t timeout_ms)
{
    uint32_t tick_start = 0U;

    CameraCapture_ResetState();
    cam_state.capture_mode = CAMERA_CAPTURE_MODE_FRAMEEVENT;

    cam_state.done_before_delay = cam_state.done;
    cam_state.error_before_delay = cam_state.error;

    cam_state.start_status = HAL_DCMI_Start_DMA(
        hdcmi,
        DCMI_MODE_SNAPSHOT,
        (uint32_t)cam_frame_buffer,
        CAM_FRAME_WORDS
    );

    if (cam_state.start_status != HAL_OK)
    {
        cam_state.error = 1;
        cam_state.error_after_delay = cam_state.error;
        HAL_DCMI_Stop(hdcmi);
        cam_state.done_after_stop = cam_state.done;
        cam_state.error_after_stop = cam_state.error;
        return HAL_ERROR;
    }

    /* JPEG 通常小于 DMA 缓冲区，HAL 的 DMA 完成回调未必触发，因此这里显式打开帧中断。 */
    __HAL_DCMI_ENABLE_IT(hdcmi, DCMI_IT_FRAME);

    tick_start = HAL_GetTick();

    while (cam_state.done == 0U && cam_state.error == 0U)
    {
        if ((HAL_GetTick() - tick_start) > timeout_ms)
        {
            cam_state.timeout = 1U;
            break;
        }
    }

    cam_state.done_after_delay = cam_state.done;
    cam_state.error_after_delay = cam_state.error;

    HAL_DCMI_Stop(hdcmi);

    cam_state.done_after_stop = cam_state.done;
    cam_state.error_after_stop = cam_state.error;

    if (cam_state.done != 1U || cam_state.error != 0U || cam_state.timeout != 0U)
    {
        return HAL_ERROR;
    }

    cam_state.wait_result = 1U;

    CameraCapture_FindJPEGMarkers();

    if (cam_state.soi_index != 0U ||
        cam_state.eoi_index == 0xFFFFFFFFU ||
        cam_state.eoi_index <= cam_state.soi_index ||
        cam_state.jpg_len == 0U)
    {
        cam_state.jpg_len = 0U;
        return HAL_ERROR;
    }

    return cam_state.start_status;
}
