/* Exercise ownership and actual queued UI task dispatch at the RTOS boundary. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "ui.h"

static unsigned char queue_items[16][128];
static size_t queue_size, queue_count, queue_head;
static TaskFunction_t ui_task;
static jmp_buf task_done;
static int queue_fail, task_fail, send_fail, allocation_fail;
static int errors, queues_deleted, allocations, render_calls, in_ui_task;
static char drawn_text[64];
static const font_t font = {NULL, NULL, NULL, 16};

static void drain_ui(void)
{
    in_ui_task = 1;
    if (setjmp(task_done) == 0) ui_task(NULL);
    in_ui_task = 0;
}

void Error_Handler(void) { ++errors; }
void *pvPortMalloc(size_t size)
{
    if (allocation_fail) return NULL;
    void *ptr = malloc(size);
    assert(ptr != NULL);
    ++allocations;
    return ptr;
}
void vPortFree(void *ptr)
{
    assert(ptr != NULL);
    --allocations;
    free(ptr);
}
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    assert(length == 16 && item_size <= sizeof(queue_items[0]));
    queue_size = item_size;
    queue_count = queue_head = 0;
    return queue_fail ? NULL : queue_items;
}
void vQueueDelete(QueueHandle_t queue)
{
    assert(queue == queue_items);
    ++queues_deleted;
}
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                       void *param, UBaseType_t priority, TaskHandle_t *handle)
{
    (void)name; (void)depth; (void)param; (void)priority; (void)handle;
    if (task_fail) return pdFAIL;
    ui_task = function;
    return pdPASS;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t timeout)
{
    assert(queue == queue_items && timeout == portMAX_DELAY);
    assert(!in_ui_task);
    if (send_fail) return pdFAIL;
    /* Simulate scheduling the consumer while a full queue blocks a producer. */
    if (queue_count == 16) drain_ui();
    memcpy(queue_items[(queue_head + queue_count) % 16], item, queue_size);
    ++queue_count;
    return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout)
{
    assert(queue == queue_items && timeout == portMAX_DELAY);
    if (queue_count == 0) longjmp(task_done, 1);
    memcpy(item, queue_items[queue_head], queue_size);
    queue_head = (queue_head + 1) % 16;
    --queue_count;
    return pdPASS;
}
void st7789_init(void) { assert(in_ui_task); }
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    assert(in_ui_task);
    assert(x1 == 1 && y1 == 2 && x2 == 3 && y2 == 4 && color == 5);
    ++render_calls;
}
void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    assert(in_ui_task && x == 6 && y == 7 && image != NULL);
    ++render_calls;
}
void st7789_write_string(uint16_t x, uint16_t y, const char *str,
                         uint16_t color, uint16_t bg_color, const font_t *resource)
{
    assert(in_ui_task && x == 8 && y == 9 && color == 10 && bg_color == 11 && resource == &font);
    assert(strlen(str) < sizeof(drawn_text));
    strcpy(drawn_text, str);
    ++render_calls;
}

int main(void)
{
    queue_fail = 1;
    ui_init();
    assert(errors == 1 && ui_task == NULL);
    queue_fail = 0;
    task_fail = 1;
    ui_init();
    assert(errors == 2 && queues_deleted == 1 && ui_task == NULL);
    task_fail = 0;
    ui_init();
    assert(ui_task != NULL);

    char temporary[] = "original";
    ui_write_string(8, 9, temporary, 10, 11, &font);
    strcpy(temporary, "modified");
    assert(allocations == 1 && render_calls == 0);
    drain_ui();
    assert(strcmp(drawn_text, "original") == 0 && allocations == 0);

    send_fail = 1;
    ui_write_string(8, 9, "failed enqueue", 10, 11, &font);
    assert(allocations == 0 && errors == 3);
    send_fail = 0;
    allocation_fail = 1;
    ui_write_string(8, 9, "out of memory", 10, 11, &font);
    assert(allocations == 0 && queue_count == 0 && errors == 4);
    allocation_fail = 0;

    ui_write_string(8, 9, NULL, 10, 11, &font);
    ui_write_string(8, 9, "null font", 10, 11, NULL);
    ui_draw_image(0, 0, NULL);
    assert(queue_count == 0 && allocations == 0);

    for (int i = 0; i < 20; ++i) ui_fill_color(1, 2, 3, 4, 5);
    const image_t image = {1, 1, (const uint8_t *)"xx"};
    ui_draw_image(6, 7, &image);
    drain_ui();
    assert(render_calls == 22 && allocations == 0 && queue_count == 0);
    puts("ui queue: all tests passed");
    return 0;
}
