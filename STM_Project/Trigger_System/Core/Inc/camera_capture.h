/*
 * camera_capture.h
 *
 *  Created on: Apr 30, 2026
 *      Author: linzh
 */

#ifndef INC_CAMERA_CAPTURE_H_
#define INC_CAMERA_CAPTURE_H_

#include "main.h"
#include <stdint.h>

#define CAM_FRAME_BUFFER_SIZE   (80 * 1024)
#define CAM_FRAME_WORDS  65536U

typedef struct
{
    uint8_t done;
    uint8_t error;

    uint32_t soi_index;
    uint32_t eoi_index;
    uint32_t jpg_len;

    HAL_StatusTypeDef start_status;
} CameraCapture_StateTypeDef;

extern uint32_t cam_frame_buffer[CAM_FRAME_WORDS];
extern CameraCapture_StateTypeDef cam_state;
extern uint32_t jpeg_buffer[];

HAL_StatusTypeDef CameraCapture_StartSnapshot(DCMI_HandleTypeDef *hdcmi);
uint8_t CameraCapture_WaitUntilDone(uint32_t timeout_ms);
void CameraCapture_FindJPEGMarkers(void);

HAL_StatusTypeDef CameraCapture_SnapshotByDelay(DCMI_HandleTypeDef *hdcmi);

#endif /* INC_CAMERA_CAPTURE_H_ */
