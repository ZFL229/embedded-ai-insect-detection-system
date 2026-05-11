/*
 * crc32.h
 *
 *  Created on: May 8, 2026
 *      Author: linzh
 */

#ifndef INC_CRC32_H_
#define INC_CRC32_H_

#include <stdint.h>

uint32_t CRC32_Calc(const uint8_t *data, uint32_t length);

#endif /* INC_CRC32_H_ */
