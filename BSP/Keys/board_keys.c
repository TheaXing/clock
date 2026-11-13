#include "board_keys.h"
#include "main.h"
#include <stdbool.h>

#define KEY_DEBOUNCE_MS 30U
#define KEY_LONG_MS 800U

typedef struct {
    bool candidate_pressed;
    bool stable_pressed;
    bool armed;
    bool long_sent;
    uint32_t candidate_since;
    uint32_t pressed_since;
} key_state_t;

static key_state_t keys[3];
static bool timer_started;

static bool is_pressed(unsigned index)
{
    GPIO_TypeDef *port = index == 0U ? GPIOA : GPIOC;
    uint16_t pin = index == 0U ? GPIO_PIN_0 : (index == 1U ? GPIO_PIN_4 : GPIO_PIN_5);
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

void board_keys_init(void)
{
    GPIO_InitTypeDef config = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /* External 10k pull-ups: all three keys connect to GND when pressed. */
    config.Mode = GPIO_MODE_INPUT;
    config.Pull = GPIO_NOPULL;
    config.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &config);
    config.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &config);
    for (unsigned i = 0U; i < 3U; ++i)
    {
        bool pressed = is_pressed(i);
        keys[i] = (key_state_t){0};
        keys[i].candidate_pressed = pressed;
        keys[i].stable_pressed = pressed;
        keys[i].armed = !pressed;
    }
    timer_started = false;
}

uint32_t board_keys_poll(uint32_t now_ms)
{
    uint32_t events = 0U;
    for (unsigned i = 0U; i < 3U; ++i)
    {
        key_state_t *key = &keys[i];
        bool pressed = is_pressed(i);
        if (!timer_started)
        {
            key->candidate_since = now_ms;
            key->pressed_since = now_ms;
        }
        if (pressed != key->candidate_pressed)
        {
            key->candidate_pressed = pressed;
            key->candidate_since = now_ms;
        }
        if (key->candidate_pressed != key->stable_pressed &&
            (uint32_t)(now_ms - key->candidate_since) >= KEY_DEBOUNCE_MS)
        {
            key->stable_pressed = key->candidate_pressed;
            if (key->stable_pressed)
            {
                key->pressed_since = now_ms;
                key->long_sent = false;
            }
            else
            {
                if (key->armed && !key->long_sent)
                {
                    events |= 1UL << i;
                }
                key->armed = true;
            }
        }
        if (key->stable_pressed && pressed && key->armed && !key->long_sent &&
            (uint32_t)(now_ms - key->pressed_since) >= KEY_LONG_MS)
        {
            events |= 1UL << (i + 8U);
            key->long_sent = true;
        }
    }
    timer_started = true;
    return events;
}
