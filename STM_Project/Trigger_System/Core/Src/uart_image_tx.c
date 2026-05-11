/*
 * uart_image_tx.c
 *
 *  Created on: May 8, 2026
 *      Author: linzh
 */

#include "uart_image_tx.h"
#include <string.h>
#include "crc32.h"

#define FRAME_SOF_0  0xAA
#define FRAME_SOF_1  0x55
#define FRAME_EOF_0  0x55
#define FRAME_EOF_1  0xAA
#define FRAME_VER    0x01

#define UART_IMG_HANDSHAKE_REQ      "STANDBY\r\n"
#define UART_IMG_HANDSHAKE_ACK      "READY"
#define UART_IMG_HANDSHAKE_PERIOD   500

#define FRAME_HEADER_SIZE 10
#define FRAME_TAIL_SIZE   6
#define CHUNK_HEADER_SIZE 5

static uint8_t tx_frame_buf[
    FRAME_HEADER_SIZE +
    CHUNK_HEADER_SIZE +
    UART_IMG_CHUNK_DATA_SIZE +
    FRAME_TAIL_SIZE
];

HAL_StatusTypeDef UART_Image_WaitForPCReady(UART_HandleTypeDef *huart)
{
    const uint8_t req_msg[] = UART_IMG_HANDSHAKE_REQ;
    const char ack_msg[] = UART_IMG_HANDSHAKE_ACK;

    uint8_t rx_byte = 0;
    uint8_t ack_index = 0;
    uint32_t last_req_tick = 0;

    if (huart == NULL)
    {
        return HAL_ERROR;
    }

    while (1)
    {
        if ((HAL_GetTick() - last_req_tick) >= UART_IMG_HANDSHAKE_PERIOD)
        {
            HAL_UART_Transmit(
                huart,
                (uint8_t *)req_msg,
                sizeof(req_msg) - 1,
                1000
            );

            last_req_tick = HAL_GetTick();
        }

        if (HAL_UART_Receive(huart, &rx_byte, 1, 50) == HAL_OK)
        {
            if (rx_byte == ack_msg[ack_index])
            {
                ack_index++;

                if (ack_index >= 5)
                {
                    return HAL_OK;
                }
            }
            else
            {
                ack_index = (rx_byte == ack_msg[0]) ? 1 : 0;
            }
        }
    }
}

HAL_StatusTypeDef UART_Image_SendJpeg(
    UART_HandleTypeDef *huart,
    uint8_t img_id,
    const uint8_t *jpeg_buf,
    uint32_t jpeg_len
)
{
    uint32_t offset = 0;
    uint16_t frame_seq = 0;
    uint16_t chunk_seq = 0;

    if (huart == NULL || jpeg_buf == NULL || jpeg_len == 0)
    {
        return HAL_ERROR;
    }

    uint16_t chunk_total =
        (jpeg_len + UART_IMG_CHUNK_DATA_SIZE - 1) / UART_IMG_CHUNK_DATA_SIZE;

    while (offset < jpeg_len)
    {
        uint16_t chunk_len = UART_IMG_CHUNK_DATA_SIZE;

        if ((jpeg_len - offset) < UART_IMG_CHUNK_DATA_SIZE)
        {
            chunk_len = (uint16_t)(jpeg_len - offset);
        }

        uint32_t payload_len = CHUNK_HEADER_SIZE + chunk_len;

        /*
         * 底层 frame header:
         * SOF(2) | VER(1) | TYPE(1) | SEQ(2) | LEN(4)
         */
        tx_frame_buf[0] = FRAME_SOF_0;
        tx_frame_buf[1] = FRAME_SOF_1;

        tx_frame_buf[2] = FRAME_VER;
        tx_frame_buf[3] = UART_IMG_TYPE_IMAGE_CHUNK;

        tx_frame_buf[4] = frame_seq & 0xFF;
        tx_frame_buf[5] = (frame_seq >> 8) & 0xFF;

        tx_frame_buf[6] = payload_len & 0xFF;
        tx_frame_buf[7] = (payload_len >> 8) & 0xFF;
        tx_frame_buf[8] = (payload_len >> 16) & 0xFF;
        tx_frame_buf[9] = (payload_len >> 24) & 0xFF;

        /*
         * IMAGE_CHUNK payload:
         * IMG_ID(1) | CHUNK_SEQ(2) | CHUNK_TOTAL(2) | CHUNK_DATA(n)
         */
        tx_frame_buf[10] = img_id;

        tx_frame_buf[11] = chunk_seq & 0xFF;
        tx_frame_buf[12] = (chunk_seq >> 8) & 0xFF;

        tx_frame_buf[13] = chunk_total & 0xFF;
        tx_frame_buf[14] = (chunk_total >> 8) & 0xFF;

        memcpy(&tx_frame_buf[15], &jpeg_buf[offset], chunk_len);

        /*
         * CRC32 只对 payload 计算
         */
        uint32_t crc = CRC32_Calc(&tx_frame_buf[10], payload_len);

        uint32_t crc_pos = FRAME_HEADER_SIZE + payload_len;

        tx_frame_buf[crc_pos + 0] = crc & 0xFF;
        tx_frame_buf[crc_pos + 1] = (crc >> 8) & 0xFF;
        tx_frame_buf[crc_pos + 2] = (crc >> 16) & 0xFF;
        tx_frame_buf[crc_pos + 3] = (crc >> 24) & 0xFF;

        tx_frame_buf[crc_pos + 4] = FRAME_EOF_0;
        tx_frame_buf[crc_pos + 5] = FRAME_EOF_1;

        uint32_t frame_len = FRAME_HEADER_SIZE + payload_len + FRAME_TAIL_SIZE;

        HAL_StatusTypeDef ret =
            HAL_UART_Transmit(huart, tx_frame_buf, frame_len, 1000);

        if (ret != HAL_OK)
        {
            return ret;
        }

        offset += chunk_len;
        chunk_seq++;
        frame_seq++;

        HAL_Delay(2);
    }

    return HAL_OK;
}
