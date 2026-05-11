/*
 * uart_image_tx.c
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

HAL_StatusTypeDef UART_Image_SendJpeg(
    UART_HandleTypeDef *huart,
    uint8_t img_id,
    const uint8_t *jpeg_buf,
    uint32_t jpeg_len
);

HAL_StatusTypeDef UART_Image_WaitForPCReady(UART_HandleTypeDef *huart);

#endif /* INC_UART_IMAGE_TX_C_ */
