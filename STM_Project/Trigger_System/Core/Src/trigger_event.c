/*
 * trigger_event.c
 *
 *  Created on: May 12, 2026
 *      Author: linzh
 */

#include "trigger_event.h"
#include "trigger_button.h"
#include "bsp_pcf8574.h"
#include "camera_capture.h"
#include "ov2640.h"
#include "uart_image_tx.h"
#include <stddef.h>
#include <stdio.h>

#define TRIGGER_EVENT_HANDSHAKE_REQ           "STANDBY\r\n"
#define TRIGGER_EVENT_HANDSHAKE_ACK           "READY"
#define TRIGGER_EVENT_TRANSFER_DONE_REQ       "TX_DONE\r\n"
#define TRIGGER_EVENT_TRANSFER_DONE_ACK       "DONE_ACK"
#define TRIGGER_EVENT_STATUS_CAPTURE_FAILED   "CAPTURE_FAILED"
#define TRIGGER_EVENT_STATUS_IMAGE_ABNORMAL   "IMAGE_ABNORMAL"
#define TRIGGER_EVENT_STATUS_TRANSFER_FAILED  "TRANSFER_FAILED"
#define TRIGGER_EVENT_HANDSHAKE_PERIOD_MS     500U
#define TRIGGER_EVENT_HANDSHAKE_TIMEOUT_MS    10000U
#define TRIGGER_EVENT_DONE_TIMEOUT_MS         3000U
#define TRIGGER_EVENT_CAPTURE_TIMEOUT_MS      2000U
#define TRIGGER_EVENT_CAPTURE_MODE            CAMERA_CAPTURE_MODE_FRAMEEVENT
/* 可切换为固定延时截取或 FrameEvent 截取，当前稳定基线使用 FrameEvent。 */
#define TRIGGER_EVENT_MIN_VALID_CAPTURE_LEN   40000U
#define TRIGGER_EVENT_BASELINE_LIGHT_MODE     6U
#define TRIGGER_EVENT_WARMUP_FRAME_COUNT      3U

/*
 * 模块职责：
 * - 将 PH2 按键事件转换成一次完整的图像采集、健康检查、传输和对账流程。
 * - 正常图像进入 UART 图像传输协议；异常图像只输出状态日志，不进入图片传输。
 *
 * 当前稳定基线：
 * - 不修改 UART 帧结构、chunk 协议或 PC 端握手机制。
 * - 图像发送完成后保留 TX_DONE / DONE_ACK 终止握手。
 * - 采集成功后、传输前使用 capture_len 做图像健康检查。
 * - capture_len 低于阈值时丢弃当前帧，并按 Soft Reset / PWDN Reset 二级恢复。
 * - 采集失败、图像异常、传输失败均向 PC 输出英文状态标签，便于压力测试观察。
 */
static DCMI_HandleTypeDef *g_trigger_hdcmi = NULL;
static UART_HandleTypeDef *g_trigger_huart = NULL;
static uint8_t g_trigger_img_id = 1U;
static uint32_t g_trigger_last_capture_len = 0U;

typedef enum
{
    TRIGGER_EVENT_RECOVERY_NORMAL = 0,
    TRIGGER_EVENT_RECOVERY_SOFT_PENDING,
    TRIGGER_EVENT_RECOVERY_HARD_PENDING
} TriggerEvent_RecoveryState;

static TriggerEvent_RecoveryState g_trigger_recovery_state = TRIGGER_EVENT_RECOVERY_NORMAL;

/* 复位或重新配置 OV2640 后丢弃若干预热帧，等待曝光/白平衡/ISP 状态收敛。 */
static void TriggerEvent_RunWarmupCaptures(void)
{
    if (g_trigger_hdcmi == NULL)
    {
        return;
    }

    for (uint8_t i = 0U; i < TRIGGER_EVENT_WARMUP_FRAME_COUNT; i++)
    {
#if TRIGGER_EVENT_CAPTURE_MODE == CAMERA_CAPTURE_MODE_FRAMEEVENT
        (void)CameraCapture_SnapshotByFrameEvent(g_trigger_hdcmi, TRIGGER_EVENT_CAPTURE_TIMEOUT_MS);
#else
        (void)CameraCapture_SnapshotByDelay(g_trigger_hdcmi);
#endif
    }

    CameraCapture_ClearBuffer();
}

/* 恢复当前稳定摄像头配置；必要时同步执行预热采集并清空采集缓冲区。 */
static void TriggerEvent_ApplyBaselineCameraConfig(uint8_t clear_capture_buffer)
{
    (void)OV2640_Init_1280x960_JPEG();
    (void)OV2640_SetLightMode(TRIGGER_EVENT_BASELINE_LIGHT_MODE);
    HAL_Delay(300);

    if (clear_capture_buffer != 0U)
    {
        TriggerEvent_RunWarmupCaptures();
    }
}

/* 一级恢复：通过 SCCB 软件复位重建 OV2640 内部寄存器状态。 */
static void TriggerEvent_SoftRecovery(uint8_t clear_capture_buffer)
{
    (void)OV2640_SoftwareReset();
    TriggerEvent_ApplyBaselineCameraConfig(clear_capture_buffer);
}

/* 二级恢复：通过 PWDN 和 RESET 对摄像头执行硬恢复。 */
static void TriggerEvent_HardRecovery(uint8_t clear_capture_buffer)
{
    BSP_PCF8574_CameraPowerDown();
    HAL_Delay(50);
    BSP_PCF8574_CameraPowerOn();
    HAL_Delay(300);

    OV2640_HardwareReset();
    TriggerEvent_ApplyBaselineCameraConfig(clear_capture_buffer);
}

/* 基于最近一次 JPEG 长度的采集链路恢复状态机。 */
static void TriggerEvent_FullAcquisitionChainRebuild(uint8_t clear_capture_buffer)
{
    if (g_trigger_last_capture_len >= TRIGGER_EVENT_MIN_VALID_CAPTURE_LEN)
    {
        g_trigger_recovery_state = TRIGGER_EVENT_RECOVERY_NORMAL;
        return;
    }

    if (g_trigger_recovery_state == TRIGGER_EVENT_RECOVERY_NORMAL)
    {
        TriggerEvent_SoftRecovery(clear_capture_buffer);
        g_trigger_recovery_state = TRIGGER_EVENT_RECOVERY_SOFT_PENDING;
        return;
    }

    TriggerEvent_HardRecovery(clear_capture_buffer);
    g_trigger_recovery_state = TRIGGER_EVENT_RECOVERY_HARD_PENDING;
}

/* 判断当前缓冲区中是否存在可发送的 JPEG 数据。 */
static uint8_t TriggerEvent_HasValidJpeg(void)
{
    return (cam_state.jpg_len > 0U) ? 1U : 0U;
}

/* 向 PC 端输出轻量级状态日志，不进入图片传输握手。 */
static HAL_StatusTypeDef TriggerEvent_SendStatusLog(const char *status)
{
    char log_buf[96];
    int log_len;

    if (g_trigger_huart == NULL || status == NULL)
    {
        return HAL_ERROR;
    }

    log_len = snprintf(
        log_buf,
        sizeof(log_buf),
        "[%s] img_id=%u capture_len=%lu\r\n",
        status,
        (unsigned int)g_trigger_img_id,
        (unsigned long)g_trigger_last_capture_len
    );

    if (log_len <= 0)
    {
        return HAL_ERROR;
    }

    if (log_len >= (int)sizeof(log_buf))
    {
        log_len = (int)sizeof(log_buf) - 1;
    }

    return HAL_UART_Transmit(
        g_trigger_huart,
        (uint8_t *)log_buf,
        (uint16_t)log_len,
        1000U
    );
}

/*
 * 传输开始握手。
 * MCU 周期性发送 STANDBY，直到 PC 回复 READY 后才开始发送图像数据。
 */
static HAL_StatusTypeDef TriggerEvent_WaitForPCReady(void)
{
    const uint8_t req_msg[] = TRIGGER_EVENT_HANDSHAKE_REQ;
    const char ack_msg[] = TRIGGER_EVENT_HANDSHAKE_ACK;
    const uint8_t ack_len = (uint8_t)(sizeof(TRIGGER_EVENT_HANDSHAKE_ACK) - 1U);

    uint8_t rx_byte = 0U;
    uint8_t ack_index = 0U;
    uint32_t start_tick = 0U;
    uint32_t last_req_tick = 0U;

    if (g_trigger_huart == NULL)
    {
        return HAL_ERROR;
    }

    start_tick = HAL_GetTick();
    last_req_tick = start_tick - TRIGGER_EVENT_HANDSHAKE_PERIOD_MS;

    while ((HAL_GetTick() - start_tick) < TRIGGER_EVENT_HANDSHAKE_TIMEOUT_MS)
    {
        if ((HAL_GetTick() - last_req_tick) >= TRIGGER_EVENT_HANDSHAKE_PERIOD_MS)
        {
            if (HAL_UART_Transmit(
                    g_trigger_huart,
                    (uint8_t *)req_msg,
                    sizeof(req_msg) - 1U,
                    1000U
                ) != HAL_OK)
            {
                return HAL_ERROR;
            }

            last_req_tick = HAL_GetTick();
        }

        if (HAL_UART_Receive(g_trigger_huart, &rx_byte, 1U, 50U) == HAL_OK)
        {
            if (rx_byte == (uint8_t)ack_msg[ack_index])
            {
                ack_index++;

                if (ack_index >= ack_len)
                {
                    return HAL_OK;
                }
            }
            else
            {
                ack_index = (rx_byte == (uint8_t)ack_msg[0]) ? 1U : 0U;
            }
        }
    }

    return HAL_TIMEOUT;
}

/* 图像发送结束通知：告知 PC 本轮传输已经结束。 */
static HAL_StatusTypeDef TriggerEvent_NotifyTransferDone(void)
{
    const uint8_t done_msg[] = TRIGGER_EVENT_TRANSFER_DONE_REQ;

    if (g_trigger_huart == NULL)
    {
        return HAL_ERROR;
    }

    return HAL_UART_Transmit(
        g_trigger_huart,
        (uint8_t *)done_msg,
        sizeof(done_msg) - 1U,
        1000U
    );
}

/* 发送长度对账日志，用于比对采集长度、发送长度、PC 接收长度和保存文件大小。 */
static HAL_StatusTypeDef TriggerEvent_SendAccountLog(uint8_t img_id, uint32_t tx_len, uint8_t tx_status)
{
    char log_buf[224];
    int log_len;

    if (g_trigger_huart == NULL)
    {
        return HAL_ERROR;
    }

    log_len = snprintf(
        log_buf,
        sizeof(log_buf),
        "[MCU_ACCOUNT] img_id=%u capture_len=%lu tx_len=%lu "
        "tx_status=%u mode=%u wait=%u timeout=%u done_bd=%u err_bd=%u done_ad=%u err_ad=%u done_stop=%u err_stop=%u "
        "soi=%lu eoi=%lu\r\n",
        (unsigned int)img_id,
        (unsigned long)g_trigger_last_capture_len,
        (unsigned long)tx_len,
        (unsigned int)tx_status,
        (unsigned int)cam_state.capture_mode,
        (unsigned int)cam_state.wait_result,
        (unsigned int)cam_state.timeout,
        (unsigned int)cam_state.done_before_delay,
        (unsigned int)cam_state.error_before_delay,
        (unsigned int)cam_state.done_after_delay,
        (unsigned int)cam_state.error_after_delay,
        (unsigned int)cam_state.done_after_stop,
        (unsigned int)cam_state.error_after_stop,
        (unsigned long)cam_state.soi_index,
        (unsigned long)cam_state.eoi_index
    );

    if (log_len <= 0)
    {
        return HAL_ERROR;
    }

    if (log_len >= (int)sizeof(log_buf))
    {
        log_len = (int)sizeof(log_buf) - 1;
    }

    return HAL_UART_Transmit(
        g_trigger_huart,
        (uint8_t *)log_buf,
        (uint16_t)log_len,
        1000U
    );
}

/* 发送采集事件日志，记录 FrameEvent、错误、超时和 JPEG marker 结果。 */
static HAL_StatusTypeDef TriggerEvent_SendCaptureEventLog(uint8_t img_id)
{
    char log_buf[192];
    int log_len;

    if (g_trigger_huart == NULL)
    {
        return HAL_ERROR;
    }

    log_len = snprintf(
        log_buf,
        sizeof(log_buf),
        "[CAP_EVT] img_id=%u done=%u error=%u timeout=%u soi=%lu eoi=%lu jpg_len=%lu\r\n",
        (unsigned int)img_id,
        (unsigned int)cam_state.done,
        (unsigned int)cam_state.error,
        (unsigned int)cam_state.timeout,
        (unsigned long)cam_state.soi_index,
        (unsigned long)cam_state.eoi_index,
        (unsigned long)cam_state.jpg_len
    );

    if (log_len <= 0)
    {
        return HAL_ERROR;
    }

    if (log_len >= (int)sizeof(log_buf))
    {
        log_len = (int)sizeof(log_buf) - 1;
    }

    return HAL_UART_Transmit(
        g_trigger_huart,
        (uint8_t *)log_buf,
        (uint16_t)log_len,
        1000U
    );
}

/* 等待 PC 对 TX_DONE 的 DONE_ACK 应答，使通信模块回到下一轮待机状态。 */
static HAL_StatusTypeDef TriggerEvent_WaitForTransferDoneAck(void)
{
    const char ack_msg[] = TRIGGER_EVENT_TRANSFER_DONE_ACK;
    const uint8_t ack_len = (uint8_t)(sizeof(TRIGGER_EVENT_TRANSFER_DONE_ACK) - 1U);

    uint8_t rx_byte = 0U;
    uint8_t ack_index = 0U;
    uint32_t start_tick = 0U;

    if (g_trigger_huart == NULL)
    {
        return HAL_ERROR;
    }

    start_tick = HAL_GetTick();

    while ((HAL_GetTick() - start_tick) < TRIGGER_EVENT_DONE_TIMEOUT_MS)
    {
        if (HAL_UART_Receive(g_trigger_huart, &rx_byte, 1U, 50U) == HAL_OK)
        {
            if (rx_byte == (uint8_t)ack_msg[ack_index])
            {
                ack_index++;

                if (ack_index >= ack_len)
                {
                    return HAL_OK;
                }
            }
            else
            {
                ack_index = (rx_byte == (uint8_t)ack_msg[0]) ? 1U : 0U;
            }
        }
    }

    return HAL_TIMEOUT;
}

/*
 * 执行一次图像采集。
 * 当前通过 TRIGGER_EVENT_CAPTURE_MODE 选择 A 组时间截取或 B 组事件截取。
 */
static HAL_StatusTypeDef TriggerEvent_CaptureImage(void)
{
    if (g_trigger_hdcmi == NULL)
    {
        return HAL_ERROR;
    }

#if TRIGGER_EVENT_CAPTURE_MODE == CAMERA_CAPTURE_MODE_FRAMEEVENT
    if (CameraCapture_SnapshotByFrameEvent(g_trigger_hdcmi, TRIGGER_EVENT_CAPTURE_TIMEOUT_MS) != HAL_OK)
    {
        g_trigger_last_capture_len = cam_state.jpg_len;
        return HAL_ERROR;
    }
#else
    if (CameraCapture_SnapshotByDelay(g_trigger_hdcmi) != HAL_OK)
    {
        g_trigger_last_capture_len = cam_state.jpg_len;
        return HAL_ERROR;
    }
#endif

    g_trigger_last_capture_len = cam_state.jpg_len;
    return (TriggerEvent_HasValidJpeg() == 1U) ? HAL_OK : HAL_ERROR;
}

/*
 * 发送当前缓冲区中的 JPEG。
 * 本函数只处理正常图片传输；异常帧已在进入本函数前被拦截。
 */
static HAL_StatusTypeDef TriggerEvent_SendCurrentImage(void)
{
    uint32_t tx_len = 0U;
    HAL_StatusTypeDef tx_status = HAL_ERROR;

    if (g_trigger_huart == NULL)
    {
        return HAL_ERROR;
    }

    if (TriggerEvent_HasValidJpeg() == 0U)
    {
        (void)TriggerEvent_SendCaptureEventLog(g_trigger_img_id);
        (void)TriggerEvent_SendAccountLog(g_trigger_img_id, 0U, 2U);
        return HAL_ERROR;
    }

    if (TriggerEvent_WaitForPCReady() != HAL_OK)
    {
        return HAL_ERROR;
    }

    tx_len = cam_state.jpg_len;

    tx_status = UART_Image_SendJpeg(
        g_trigger_huart,
        g_trigger_img_id,
        (uint8_t *)cam_frame_buffer,
        tx_len
    );

    (void)TriggerEvent_SendCaptureEventLog(g_trigger_img_id);
    (void)TriggerEvent_SendAccountLog(
        g_trigger_img_id,
        (tx_status == HAL_OK) ? tx_len : 0U,
        (tx_status == HAL_OK) ? 0U : 1U
    );

    if (tx_status != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (TriggerEvent_NotifyTransferDone() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (TriggerEvent_WaitForTransferDoneAck() != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

static void TriggerEvent_CaptureAndSend(void)
{
    uint8_t image_abnormal = 0U;

    if (TriggerEvent_CaptureImage() != HAL_OK)
    {
        (void)TriggerEvent_SendStatusLog(TRIGGER_EVENT_STATUS_CAPTURE_FAILED);
        TriggerEvent_FullAcquisitionChainRebuild(1U);
        return;
    }

    /* 拍摄成功后、传输前先做健康检查，异常帧在 MCU 端直接丢弃。 */
    image_abnormal = (g_trigger_last_capture_len < TRIGGER_EVENT_MIN_VALID_CAPTURE_LEN) ? 1U : 0U;
    TriggerEvent_FullAcquisitionChainRebuild(image_abnormal);

    if (image_abnormal != 0U)
    {
        (void)TriggerEvent_SendStatusLog(TRIGGER_EVENT_STATUS_IMAGE_ABNORMAL);
        return;
    }

    if (TriggerEvent_SendCurrentImage() != HAL_OK)
    {
        (void)TriggerEvent_SendStatusLog(TRIGGER_EVENT_STATUS_TRANSFER_FAILED);
        CameraCapture_ClearBuffer();
        return;
    }

    g_trigger_img_id++;
}

/* 初始化触发事件模块，保存外设句柄并复位按键状态。 */
void TriggerEvent_Init(DCMI_HandleTypeDef *hdcmi, UART_HandleTypeDef *huart)
{
    g_trigger_hdcmi = hdcmi;
    g_trigger_huart = huart;
    g_trigger_img_id = 1U;
    g_trigger_last_capture_len = 0U;
    g_trigger_recovery_state = TRIGGER_EVENT_RECOVERY_NORMAL;

    TriggerButton_Init();
}

/* 主循环调度入口：PH2 单键触发采集、传输和对账。 */
void TriggerEvent_Process(void)
{
    if (TriggerButton_ScanPH2Event() == 1U)
    {
        TriggerEvent_CaptureAndSend();
    }
}
