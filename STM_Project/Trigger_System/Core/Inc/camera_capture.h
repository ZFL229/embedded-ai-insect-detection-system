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

#define CAMERA_CAPTURE_MODE_DELAY       0U
#define CAMERA_CAPTURE_MODE_FRAMEEVENT  1U

/*
 * 摄像头采集状态记录。
 * 该结构体同时保存 JPEG 定位结果和实验诊断字段，供采集、发送和串口日志共用。
 */
typedef struct
{
    /* DCMI 帧完成回调和错误回调置位的基础状态。 */
    uint8_t done;
    uint8_t error;

    /* 在采集缓冲区中扫描到的 JPEG SOI/EOI 位置和最终 JPEG 长度。 */
    uint32_t soi_index;
    uint32_t eoi_index;
    uint32_t jpg_len;

    /* 采集流程关键时刻的状态快照，用于判断事件、超时和停止时序。 */
    uint8_t done_before_delay;
    uint8_t error_before_delay;
    uint8_t done_after_delay;
    uint8_t error_after_delay;
    uint8_t done_after_stop;
    uint8_t error_after_stop;
    uint8_t capture_mode;
    uint8_t wait_result;
    uint8_t timeout;

    HAL_StatusTypeDef start_status;
} CameraCapture_StateTypeDef;

extern uint32_t cam_frame_buffer[CAM_FRAME_WORDS];
extern CameraCapture_StateTypeDef cam_state;
extern uint32_t jpeg_buffer[];

/* 启动一次 DCMI Snapshot DMA，底层完成标志由 HAL 回调更新。 */
HAL_StatusTypeDef CameraCapture_StartSnapshot(DCMI_HandleTypeDef *hdcmi);

/* 阻塞等待采集完成或错误/超时，用于早期调试路径。 */
uint8_t CameraCapture_WaitUntilDone(uint32_t timeout_ms);

/* 在采集缓冲区中查找 JPEG SOI/EOI，并更新 cam_state.jpg_len。 */
void CameraCapture_FindJPEGMarkers(void);

/* 清空图像缓冲区并复位采集状态，不启动新的采集。 */
void CameraCapture_ClearBuffer(void);

/* A 组对照路径：固定时间窗截取一帧 JPEG。 */
HAL_StatusTypeDef CameraCapture_SnapshotByDelay(DCMI_HandleTypeDef *hdcmi);

/* B 组实验路径：等待 DCMI FrameEvent 后停止并扫描 JPEG。 */
HAL_StatusTypeDef CameraCapture_SnapshotByFrameEvent(DCMI_HandleTypeDef *hdcmi, uint32_t timeout_ms);

#endif /* INC_CAMERA_CAPTURE_H_ */
