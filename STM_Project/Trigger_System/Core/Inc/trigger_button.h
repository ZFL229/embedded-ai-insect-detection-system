/*
 * trigger_button.h
 *
 *  Created on: May 12, 2026
 *      Author: linzh
 *
 * 功能说明：
 * - 提供 PH2 和 PH3 两个按键的扫描接口。
 * - 每个按键都有独立的软件消抖状态机。
 * - 只有完成一次稳定的“按下 -> 松开”过程，才产生一次有效事件。
 */

#ifndef INC_TRIGGER_BUTTON_H_
#define INC_TRIGGER_BUTTON_H_

#include "main.h"
#include <stdint.h>

typedef enum
{
    /* 空闲状态：等待新的按下动作。 */
    TRIGGER_BUTTON_IDLE = 0,

    /* 按下消抖：检测到按下电平后，等待消抖时间再确认。 */
    TRIGGER_BUTTON_DEBOUNCE_PRESS,

    /* 已按下：按下动作已确认，等待用户松开。 */
    TRIGGER_BUTTON_PRESSED,

    /* 松开消抖：检测到松开电平后，等待消抖时间再确认。 */
    TRIGGER_BUTTON_DEBOUNCE_RELEASE
} TriggerButton_State_t;

/*
 * 初始化 PH2 和 PH3 两个按键的软件状态机。
 * 这里只复位软件状态，不配置 GPIO 硬件；GPIO 仍由 MX_GPIO_Init() 配置。
 */
void TriggerButton_Init(void);

/* 扫描 PH2，检测到一次稳定的按下并松开事件时返回 1，否则返回 0。 */
uint8_t TriggerButton_ScanPH2Event(void);

/* 扫描 PH3，检测到一次稳定的按下并松开事件时返回 1，否则返回 0。 */
uint8_t TriggerButton_ScanPH3Event(void);

#endif /* INC_TRIGGER_BUTTON_H_ */
