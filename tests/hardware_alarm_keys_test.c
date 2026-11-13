#include "board_keys.h"
#include "main.h"
#include <assert.h>
#include <stdio.h>

GPIO_TypeDef test_gpio_a, test_gpio_c;
static uint32_t down;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    uint32_t bit = port == GPIOA ? 1U : (pin == GPIO_PIN_4 ? 2U : 4U);
    return (down & bit) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *config)
{
    (void)port;
    assert(config->Mode == GPIO_MODE_INPUT && config->Pull == GPIO_NOPULL);
}
static void reset(uint32_t held) { down = held; board_keys_init(); }
static void no_event(uint32_t time) { assert(board_keys_poll(time) == 0U); }

int main(void)
{
    /* A short event requires both a stable press and a stable release. */
    reset(0U); no_event(0U);
    down = 1U; no_event(10U);
    down = 0U; no_event(20U);
    down = 1U; no_event(30U); no_event(59U); no_event(60U);
    down = 0U; no_event(100U);
    down = 1U; no_event(110U);
    down = 0U; no_event(120U); no_event(149U);
    assert(board_keys_poll(150U) == BOARD_KEY1_SHORT);
    no_event(160U);

    /* Long fires once at 800 ms after debounced press; release is silent. */
    reset(0U); no_event(0U);
    down = 2U; no_event(10U); no_event(40U); no_event(839U);
    assert(board_keys_poll(840U) == BOARD_KEY2_LONG);
    no_event(1200U); down = 0U; no_event(1210U); no_event(1240U);

    /* Startup-held keys cannot emit an event until a stable release. */
    reset(7U); no_event(0U); no_event(2000U);
    down = 0U; no_event(2010U); no_event(2040U);
    down = 7U; no_event(2050U); no_event(2080U);
    down = 0U; no_event(2100U);
    assert(board_keys_poll(2130U) == (BOARD_KEY1_SHORT | BOARD_KEY2_SHORT | BOARD_KEY3_SHORT));

    /* Timing remains correct through the uint32 millisecond rollover. */
    reset(0U); no_event(UINT32_MAX - 100U);
    down = 4U; no_event(UINT32_MAX - 50U); no_event(UINT32_MAX - 20U);
    no_event(778U); assert(board_keys_poll(779U) == BOARD_KEY3_LONG);
    down = 0U; no_event(800U); no_event(830U);
    puts("alarm hardware keys: pass");
    return 0;
}
