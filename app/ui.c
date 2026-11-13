#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "main.h"
#include "st7789.h"
#include "ui.h"

typedef enum
{
    UI_ACTION_FILL_COLOR,
    UI_ACTION_WRITE_STRING,
    UI_ACTION_DRAW_IMAGE
} ui_action_t;

typedef struct
{
    ui_action_t action;
    union
    {
        struct
        {
            uint16_t x1, y1, x2, y2, color;
        } fill_color;
        struct
        {
            uint16_t x, y;
            char *str;
            uint16_t color, bg_color;
            const font_t *font;
        } write_string;
        struct
        {
            uint16_t x, y;
            const image_t *image;
        } draw_image;
    } data;
} ui_message_t;

static QueueHandle_t ui_queue;

static void ui_func(void *param)
{
    ui_message_t msg;
    (void)param;
    st7789_init();

    for (;;)
    {
        if (xQueueReceive(ui_queue, &msg, portMAX_DELAY) != pdPASS)
            continue;

        switch (msg.action)
        {
        case UI_ACTION_FILL_COLOR:
            st7789_fill_color(msg.data.fill_color.x1, msg.data.fill_color.y1,
                              msg.data.fill_color.x2, msg.data.fill_color.y2,
                              msg.data.fill_color.color);
            break;
        case UI_ACTION_WRITE_STRING:
            st7789_write_string(msg.data.write_string.x, msg.data.write_string.y,
                                msg.data.write_string.str,
                                msg.data.write_string.color, msg.data.write_string.bg_color,
                                msg.data.write_string.font);
            vPortFree(msg.data.write_string.str);
            break;
        case UI_ACTION_DRAW_IMAGE:
            st7789_draw_image(msg.data.draw_image.x, msg.data.draw_image.y,
                              msg.data.draw_image.image);
            break;
        default:
            configASSERT(0);
            break;
        }
    }
}

void ui_init(void)
{
    if (ui_queue != NULL)
        return;

    ui_queue = xQueueCreate(16, sizeof(ui_message_t));
    if (ui_queue == NULL)
    {
        Error_Handler();
        return;
    }

    if (xTaskCreate(ui_func, "ui", 1024, NULL, 8, NULL) != pdPASS)
    {
        vQueueDelete(ui_queue);
        ui_queue = NULL;
        Error_Handler();
    }
}

/* Producers run in tasks, so blocking provides backpressure without dropping
 * a fill/glyph/image from the middle of a page redraw. Never call from an ISR,
 * timer service callback, or from ui_func itself. */
static BaseType_t ui_submit(const ui_message_t *msg)
{
    if (ui_queue == NULL)
        return pdFAIL;
    return xQueueSend(ui_queue, msg, portMAX_DELAY);
}

void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    ui_message_t msg = {0};
    msg.action = UI_ACTION_FILL_COLOR;
    msg.data.fill_color.x1 = x1;
    msg.data.fill_color.y1 = y1;
    msg.data.fill_color.x2 = x2;
    msg.data.fill_color.y2 = y2;
    msg.data.fill_color.color = color;
    if (ui_submit(&msg) != pdPASS)
        Error_Handler();
}

void ui_write_string(uint16_t x, uint16_t y, const char *str,
                     uint16_t color, uint16_t bg_color, const font_t *font)
{
    if (str == NULL || font == NULL)
        return;

    size_t length = strlen(str) + 1;
    char *copy = pvPortMalloc(length);
    if (copy == NULL)
    {
        Error_Handler();
        return;
    }
    memcpy(copy, str, length);

    ui_message_t msg = {0};
    msg.action = UI_ACTION_WRITE_STRING;
    msg.data.write_string.x = x;
    msg.data.write_string.y = y;
    msg.data.write_string.str = copy;
    msg.data.write_string.color = color;
    msg.data.write_string.bg_color = bg_color;
    msg.data.write_string.font = font;

    /* Ownership transfers only after a successful enqueue. */
    if (ui_submit(&msg) != pdPASS)
    {
        vPortFree(copy);
        Error_Handler();
    }
}

void ui_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    if (image == NULL)
        return;

    ui_message_t msg = {0};
    msg.action = UI_ACTION_DRAW_IMAGE;
    msg.data.draw_image.x = x;
    msg.data.draw_image.y = y;
    msg.data.draw_image.image = image;
    if (ui_submit(&msg) != pdPASS)
        Error_Handler();
}
