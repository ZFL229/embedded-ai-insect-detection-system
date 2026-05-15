/*
 * trigger_button.c
 *
 *  Created on: May 12, 2026
 *      Author: linzh
 *
 * 功能说明：
 * - 实现 PH2 和 PH3 两个按键的软件状态机。
 * - 将有抖动的 GPIO 输入电平转换成稳定的单次按键事件。
 * - 本模块只读取 GPIO 电平和 HAL 时间，不负责 GPIO 初始化。
 */

#include "trigger_button.h"

/* PH2 和 PH3 都在 GPIOH 端口，GPIO 输入上拉配置由 main.c 中的 MX_GPIO_Init() 完成。 */
#define TRIGGER_BUTTON_GPIO_PORT      GPIOH
#define TRIGGER_BUTTON_PH2_PIN        GPIO_PIN_2
#define TRIGGER_BUTTON_PH3_PIN        GPIO_PIN_3

/* 当前硬件按上拉输入处理：低电平表示按下，高电平表示松开。 */
#define TRIGGER_BUTTON_PRESSED_LEVEL  GPIO_PIN_RESET
#define TRIGGER_BUTTON_RELEASED_LEVEL GPIO_PIN_SET

/* 软件消抖时间：电平保持稳定达到该时间后，才确认状态变化有效。 */
#define TRIGGER_BUTTON_DEBOUNCE_MS    20U

/*
 * 单个按键的运行上下文。
 * pin   : 当前按键对应的 GPIO 引脚。
 * state : 当前按键状态机所处状态。
 * tick  : 进入消抖状态时记录的 HAL 时间戳。
 */
typedef struct
{
    uint16_t pin;
    TriggerButton_State_t state;
    uint32_t tick;
} TriggerButton_Context_t;

/* PH2 按键的独立状态机上下文。 */
static TriggerButton_Context_t g_ph2_button = {
    TRIGGER_BUTTON_PH2_PIN,
    TRIGGER_BUTTON_IDLE,
    0U
};

/* PH3 按键的独立状态机上下文。 */
static TriggerButton_Context_t g_ph3_button = {
    TRIGGER_BUTTON_PH3_PIN,
    TRIGGER_BUTTON_IDLE,
    0U
};

/*
 * 复位两个按键的软件状态机。
 * 注意：本函数不初始化 GPIO 硬件，只清空软件状态。
 */
void TriggerButton_Init(void)
{
    g_ph2_button.state = TRIGGER_BUTTON_IDLE;
    g_ph2_button.tick = 0U;

    g_ph3_button.state = TRIGGER_BUTTON_IDLE;
    g_ph3_button.tick = 0U;
}

/* 封装 GPIO 读取，让状态机不直接散落具体引脚读取代码。 */
static GPIO_PinState TriggerButton_ReadLevel(const TriggerButton_Context_t *button)
{
    return HAL_GPIO_ReadPin(TRIGGER_BUTTON_GPIO_PORT, button->pin);
}

/*
 * 通用按键扫描状态机。
 * 返回 1 表示完成一次稳定的“按下 -> 松开”事件。
 */
static uint8_t TriggerButton_ScanEvent(TriggerButton_Context_t *button)
{
    uint8_t event = 0U;
    uint32_t now = HAL_GetTick();

    switch (button->state)
    {
        case TRIGGER_BUTTON_IDLE:
        {
            /* 检测到可能的按下动作，先进入按下消抖。 */
            if (TriggerButton_ReadLevel(button) == TRIGGER_BUTTON_PRESSED_LEVEL)
            {
                button->tick = now;
                button->state = TRIGGER_BUTTON_DEBOUNCE_PRESS;
            }
            break;
        }

        case TRIGGER_BUTTON_DEBOUNCE_PRESS:
        {
            /* 消抖时间到后再次读取电平，确认按下是否稳定。 */
            if ((now - button->tick) >= TRIGGER_BUTTON_DEBOUNCE_MS)
            {
                if (TriggerButton_ReadLevel(button) == TRIGGER_BUTTON_PRESSED_LEVEL)
                {
                    button->state = TRIGGER_BUTTON_PRESSED;
                }
                else
                {
                    button->state = TRIGGER_BUTTON_IDLE;
                }
            }
            break;
        }

        case TRIGGER_BUTTON_PRESSED:
        {
            /* 按下已确认，此状态只等待松开，不重复产生事件。 */
            if (TriggerButton_ReadLevel(button) == TRIGGER_BUTTON_RELEASED_LEVEL)
            {
                button->tick = now;
                button->state = TRIGGER_BUTTON_DEBOUNCE_RELEASE;
            }
            break;
        }

        case TRIGGER_BUTTON_DEBOUNCE_RELEASE:
        {
            /* 松开稳定后输出一次事件；若电平回落，则回到已按下状态。 */
            if ((now - button->tick) >= TRIGGER_BUTTON_DEBOUNCE_MS)
            {
                if (TriggerButton_ReadLevel(button) == TRIGGER_BUTTON_RELEASED_LEVEL)
                {
                    event = 1U;
                    button->state = TRIGGER_BUTTON_IDLE;
                }
                else
                {
                    button->state = TRIGGER_BUTTON_PRESSED;
                }
            }
            break;
        }

        default:
        {
            /* 异常保护：状态值异常时恢复到空闲状态，避免状态机卡死。 */
            button->state = TRIGGER_BUTTON_IDLE;
            button->tick = 0U;
            break;
        }
    }

    return event;
}

/* PH2 对外扫描接口，内部复用通用状态机。 */
uint8_t TriggerButton_ScanPH2Event(void)
{
    return TriggerButton_ScanEvent(&g_ph2_button);
}

/* PH3 对外扫描接口，内部复用通用状态机。 */
uint8_t TriggerButton_ScanPH3Event(void)
{
    return TriggerButton_ScanEvent(&g_ph3_button);
}
