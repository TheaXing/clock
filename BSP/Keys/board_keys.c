#include "board_keys.h"
#include "main.h"
#include <stdbool.h>

#define KEY_DEBOUNCE_MS 30U
#define KEY_LONG_MS 800U

/** 保存单个按键的消抖、事件使能与长按计时状态。 */
typedef struct {
    bool candidate_pressed;   // 尚待消抖确认的按下状态
    bool stable_pressed;      // 已经消抖确认的按下状态
    bool armed;               // 是否已允许产生按键事件
    bool long_sent;           // 本次按下是否已上报长按
    uint32_t candidate_since; // 候选状态开始的毫秒时刻
    uint32_t pressed_since;   // 稳定按下开始的毫秒时刻
} key_state_t;

static key_state_t keys[3];
static bool timer_started;

/**
 * 功能：读取指定板载按键的低有效输入电平。
 * 参数：index，按键索引，取值为 0～2。
 * 返回：按键处于按下电平返回 true，否则返回 false。
 */
static bool is_pressed(unsigned index) {
    GPIO_TypeDef *port = index == 0U ? GPIOA : GPIOC;
    uint16_t pin = index == 0U ? GPIO_PIN_0 : (index == 1U ? GPIO_PIN_4 : GPIO_PIN_5);
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

/**
 * 功能：初始化三个外部上拉按键，并保存启动时的输入状态。
 * 参数：无。
 * 返回：无。
 */
void board_keys_init(void) {
    GPIO_InitTypeDef config = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /* 三个按键均使用外部 10 千欧上拉，按下时连接地。 */
    config.Mode = GPIO_MODE_INPUT;
    config.Pull = GPIO_NOPULL;
    config.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOA, &config);
    config.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOC, &config);
    for (unsigned i = 0U; i < 3U; ++i) {
        bool pressed = is_pressed(i);
        keys[i] = (key_state_t){0};
        keys[i].candidate_pressed = pressed;
        keys[i].stable_pressed = pressed;
        keys[i].armed = !pressed;
    }
    timer_started = false;
}

/**
 * 功能：按单调时间进行按键消抖并生成短按或一次性长按事件。
 * 参数：now_ms，当前单调递增的毫秒时间。
 * 返回：低位为短按事件，第 8 位起为对应按键长按事件的位掩码。
 */
uint32_t board_keys_poll(uint32_t now_ms) {
    uint32_t events = 0U;
    for (unsigned i = 0U; i < 3U; ++i) {
        key_state_t *key = &keys[i];
        bool pressed = is_pressed(i);
        if (!timer_started) {
            key->candidate_since = now_ms;
            key->pressed_since = now_ms;
        }
        if (pressed != key->candidate_pressed) {
            key->candidate_pressed = pressed;
            key->candidate_since = now_ms;
        }
        if (key->candidate_pressed != key->stable_pressed &&
            (uint32_t)(now_ms - key->candidate_since) >= KEY_DEBOUNCE_MS) {
            key->stable_pressed = key->candidate_pressed;
            if (key->stable_pressed) {
                key->pressed_since = now_ms;
                key->long_sent = false;
            } else {
                if (key->armed && !key->long_sent) {
                    events |= 1UL << i;
                }
                key->armed = true;
            }
        }
        if (key->stable_pressed && pressed && key->armed && !key->long_sent &&
            (uint32_t)(now_ms - key->pressed_since) >= KEY_LONG_MS) {
            events |= 1UL << (i + 8U);
            key->long_sent = true;
        }
    }
    timer_started = true;
    return events;
}
