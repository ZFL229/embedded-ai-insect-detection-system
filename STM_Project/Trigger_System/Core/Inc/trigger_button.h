/*
 * trigger_button.h
 *
 *  Created on: May 12, 2026
 *      Author: linzh
 */

#ifndef INC_TRIGGER_BUTTON_H_
#define INC_TRIGGER_BUTTON_H_

#include "main.h"
#include <stdint.h>

typedef enum
{
    TRIGGER_BUTTON_IDLE = 0,
    TRIGGER_BUTTON_DEBOUNCE_PRESS,
    TRIGGER_BUTTON_PRESSED,
    TRIGGER_BUTTON_DEBOUNCE_RELEASE
} TriggerButton_State_t;

void TriggerButton_Init(void);
uint8_t TriggerButton_ScanPH2Event(void);

#endif /* INC_TRIGGER_BUTTON_H_ */
