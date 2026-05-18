/*
 * trigger_button.c
 *
 *  Created on: May 12, 2026
 *      Author: linzh
 */

#include "trigger_button.h"

#define TRIGGER_BUTTON_GPIO_PORT      GPIOH
#define TRIGGER_BUTTON_PH2_PIN        GPIO_PIN_2

#define TRIGGER_BUTTON_PRESSED_LEVEL  GPIO_PIN_RESET
#define TRIGGER_BUTTON_RELEASED_LEVEL GPIO_PIN_SET
#define TRIGGER_BUTTON_DEBOUNCE_MS    20U

typedef struct
{
    uint16_t pin;
    TriggerButton_State_t state;
    uint32_t tick;
} TriggerButton_Context_t;

static TriggerButton_Context_t g_ph2_button = {
    TRIGGER_BUTTON_PH2_PIN,
    TRIGGER_BUTTON_IDLE,
    0U
};

void TriggerButton_Init(void)
{
    g_ph2_button.state = TRIGGER_BUTTON_IDLE;
    g_ph2_button.tick = 0U;
}

static GPIO_PinState TriggerButton_ReadLevel(const TriggerButton_Context_t *button)
{
    return HAL_GPIO_ReadPin(TRIGGER_BUTTON_GPIO_PORT, button->pin);
}

static uint8_t TriggerButton_ScanEvent(TriggerButton_Context_t *button)
{
    uint8_t event = 0U;
    uint32_t now = HAL_GetTick();

    switch (button->state)
    {
        case TRIGGER_BUTTON_IDLE:
        {
            if (TriggerButton_ReadLevel(button) == TRIGGER_BUTTON_PRESSED_LEVEL)
            {
                button->tick = now;
                button->state = TRIGGER_BUTTON_DEBOUNCE_PRESS;
            }
            break;
        }

        case TRIGGER_BUTTON_DEBOUNCE_PRESS:
        {
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
            if (TriggerButton_ReadLevel(button) == TRIGGER_BUTTON_RELEASED_LEVEL)
            {
                button->tick = now;
                button->state = TRIGGER_BUTTON_DEBOUNCE_RELEASE;
            }
            break;
        }

        case TRIGGER_BUTTON_DEBOUNCE_RELEASE:
        {
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
            button->state = TRIGGER_BUTTON_IDLE;
            button->tick = 0U;
            break;
        }
    }

    return event;
}

uint8_t TriggerButton_ScanPH2Event(void)
{
    return TriggerButton_ScanEvent(&g_ph2_button);
}
