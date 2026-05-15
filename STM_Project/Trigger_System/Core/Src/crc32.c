/*
 * crc32.c
 *
 *  Created on: May 8, 2026
 *      Author: linzh
 */

#include "crc32.h"

/*
 * 计算通信帧 payload 的 CRC32。
 * 多项式 0xEDB88320，与 PC 端 frame_codec 的校验逻辑保持一致。
 */
uint32_t CRC32_Calc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}
