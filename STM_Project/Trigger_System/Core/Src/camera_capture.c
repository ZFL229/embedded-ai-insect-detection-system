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

CameraCapture_StateTypeDef cam_state =
{
    .done = 0,
    .error = 0,
    .start_status = HAL_OK,
    .soi_index = 0xFFFFFFFF,
    .eoi_index = 0xFFFFFFFF
};

HAL_StatusTypeDef CameraCapture_StartSnapshot(DCMI_HandleTypeDef *hdcmi)
{
    memset(cam_frame_buffer, 0, sizeof(cam_frame_buffer));

    cam_state.done = 0;
    cam_state.error = 0;
    cam_state.soi_index = 0xFFFFFFFF;
    cam_state.eoi_index = 0xFFFFFFFF;

    cam_state.start_status = HAL_DCMI_Start_DMA(
        hdcmi,
        DCMI_MODE_SNAPSHOT,
        (uint32_t)cam_frame_buffer,
        CAM_FRAME_WORDS
    );

    return cam_state.start_status;
}

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

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi)
{
	cam_state.done = 1;
    HAL_DCMI_Stop(hdcmi);
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *hdcmi)
{
	cam_state.error = 1;
}

HAL_StatusTypeDef CameraCapture_SnapshotByDelay(DCMI_HandleTypeDef *hdcmi)
{
    memset(cam_frame_buffer, 0, sizeof(cam_frame_buffer));

    cam_state.done = 0;
    cam_state.error = 0;
    cam_state.soi_index = 0xFFFFFFFF;
    cam_state.eoi_index = 0xFFFFFFFF;

    cam_state.start_status = HAL_DCMI_Start_DMA(
        hdcmi,
        DCMI_MODE_SNAPSHOT,
        (uint32_t)cam_frame_buffer,
        CAM_FRAME_WORDS
    );

    HAL_Delay(500);

    HAL_DCMI_Suspend(hdcmi);
    HAL_DCMI_Stop(hdcmi);

    CameraCapture_FindJPEGMarkers();

    return cam_state.start_status;
}
