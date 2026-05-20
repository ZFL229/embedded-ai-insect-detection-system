/*
 * uart_image_tx.h
 *
 *  Created on: May 8, 2026
 *      Author: linzh
 */

#ifndef INC_UART_IMAGE_TX_C_
#define INC_UART_IMAGE_TX_C_

#include "main.h"
#include <stdint.h>


#define UART_IMG_TYPE_IMAGE_CHUNK   0x03
#define UART_IMG_CHUNK_DATA_SIZE    256

/*
 * 将一张 JPEG 按 256 字节 chunk 切分，并封装为现有 UART 图像帧发送。
 * 本接口只负责 payload 分片、CRC 和帧格式，不负责 STANDBY/READY 或 TX_DONE 握手。
 */
HAL_StatusTypeDef UART_Image_SendJpeg(
    UART_HandleTypeDef *huart,
    uint8_t img_id,
    const uint8_t *jpeg_buf,
    uint32_t jpeg_len
);

#endif /* INC_UART_IMAGE_TX_C_ */
