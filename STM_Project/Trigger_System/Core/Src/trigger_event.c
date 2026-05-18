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
#include "uart_image_tx.h"
#include <stddef.h>
#include <stdio.h>

#define TRIGGER_EVENT_HANDSHAKE_REQ           "STANDBY\r\n"
#define TRIGGER_EVENT_HANDSHAKE_ACK           "READY"
#define TRIGGER_EVENT_TRANSFER_DONE_REQ       "TX_DONE\r\n"
#define TRIGGER_EVENT_TRANSFER_DONE_ACK       "DONE_ACK"
#define TRIGGER_EVENT_HANDSHAKE_PERIOD_MS     500U
#define TRIGGER_EVENT_HANDSHAKE_TIMEOUT_MS    10000U
#define TRIGGER_EVENT_DONE_TIMEOUT_MS         3000U
#define TRIGGER_EVENT_CAPTURE_TIMEOUT_MS      2000U
#define TRIGGER_EVENT_CAPTURE_MODE            CAMERA_CAPTURE_MODE_FRAMEEVENT
// CAMERA_CAPTURE_MODE_DELAY / CAMERA_CAPTURE_MODE_FRAMEEVENT
#define TRIGGER_EVENT_FULL_REBUILD_AFTER_TX   1U

/*
 * 模块职责：
 * - 将 PH2 按键事件转换成完整图像流程。
 * - PH2：采集一帧图像到 cam_frame_buffer，随后立即发送 JPEG 并输出对账日志。
 *
 * 当前实验约束：
 * - 不修改 UART 帧结构、chunk 协议或 PC 端握手机制。
 * - 图像发送完成后保留 TX_DONE / DONE_ACK 终止握手。
 * - 可在传输结束后执行采集链路重建，用于验证状态残留问题。
 */
static DCMI_HandleTypeDef *g_trigger_hdcmi = NULL;
static UART_HandleTypeDef *g_trigger_huart = NULL;
static uint8_t g_trigger_img_id = 1U;
static uint32_t g_trigger_last_capture_len = 0U;

/*
 * 发送结束后的摄像头恢复流程。
 * 当前只通过 PWDN 让 OV2640 进行深恢复，避免 DCMI/DMA/I2C/寄存器重配带来额外副作用。
 */
static void TriggerEvent_FullAcquisitionChainRebuild(void)
{
#if TRIGGER_EVENT_FULL_REBUILD_AFTER_TX
    BSP_PCF8574_CameraPowerDown();
    HAL_Delay(50);
    BSP_PCF8574_CameraPowerOn();
    HAL_Delay(300);

    CameraCapture_ClearBuffer();
#endif
}

/* 判断当前缓冲区中是否存在可发送的 JPEG 数据。 */
static uint8_t TriggerEvent_HasValidJpeg(void)
{
    return (cam_state.jpg_len > 0U) ? 1U : 0U;
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
 * 如果没有有效 JPEG，也会完成一次 PC 握手并发送采集日志和 TX_DONE，避免 PC 端卡在接收状态。
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
    if (TriggerEvent_CaptureImage() != HAL_OK)
    {
        goto fail_cleanup;
    }

    if (TriggerEvent_SendCurrentImage() != HAL_OK)
    {
        goto fail_cleanup;
    }

    TriggerEvent_FullAcquisitionChainRebuild();
    g_trigger_img_id++;
    return;

fail_cleanup:
    TriggerEvent_FullAcquisitionChainRebuild();
}

/* 初始化触发事件模块，保存外设句柄并复位按键状态。 */
void TriggerEvent_Init(DCMI_HandleTypeDef *hdcmi, UART_HandleTypeDef *huart)
{
    g_trigger_hdcmi = hdcmi;
    g_trigger_huart = huart;
    g_trigger_img_id = 1U;
    g_trigger_last_capture_len = 0U;

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
