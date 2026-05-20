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
    /* 空闲态：等待检测到按键按下。 */
    TRIGGER_BUTTON_IDLE = 0,

    /* 按下消抖态：确认按下电平稳定。 */
    TRIGGER_BUTTON_DEBOUNCE_PRESS,

    /* 已按下态：等待用户释放按键。 */
    TRIGGER_BUTTON_PRESSED,

    /* 释放消抖态：确认释放电平稳定，随后产生一次触发事件。 */
    TRIGGER_BUTTON_DEBOUNCE_RELEASE
} TriggerButton_State_t;

/* 初始化 PH2 按键状态机。 */
void TriggerButton_Init(void);

/* 扫描 PH2 按键；完整按下并释放后返回 1，否则返回 0。 */
uint8_t TriggerButton_ScanPH2Event(void);

#endif /* INC_TRIGGER_BUTTON_H_ */
