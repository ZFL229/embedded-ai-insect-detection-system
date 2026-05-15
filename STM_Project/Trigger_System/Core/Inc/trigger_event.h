/*
 * trigger_event.h
 *
 *  Created on: May 12, 2026
 *      Author: linzh
 */

#ifndef INC_TRIGGER_EVENT_H_
#define INC_TRIGGER_EVENT_H_

#include "main.h"

/*
 * 初始化触发事件模块。
 * 保存 DCMI 和 UART 句柄，并初始化 PH2/PH3 按键状态机。
 */
void TriggerEvent_Init(DCMI_HandleTypeDef *hdcmi, UART_HandleTypeDef *huart);

/*
 * 周期性事件调度入口，应在 main while(1) 中调用。
 * PH2 触发图像采集，PH3 触发当前 JPEG 缓冲区发送。
 */
void TriggerEvent_Process(void);

#endif /* INC_TRIGGER_EVENT_H_ */
