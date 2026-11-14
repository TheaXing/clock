#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "ui.h"
#include "ota_service.h"

static TaskFunction_t draw_task;
static jmp_buf stopped;
static uint8_t queued[128];
static size_t queued_size;
static bool queued_full, idle_once, blocked_draw;
static unsigned pulses, renders;

/** 功能：运行真实界面工作任务直至边界停止；参数：无；返回：无。 */
static void run_ui(void) {
    if (setjmp(stopped) == 0) {
        draw_task(NULL);
    }
}

/** 功能：记录真实 UI 任务在绘制完成或空闲等待后报告的进展；参数：source，来源；返回：无。 */
void ota_service_heartbeat(uint8_t source) {
    assert(source == OTA_HEARTBEAT_UI && !queued_full);
    ++pulses;
}

/** 功能：注册界面任务；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint16_t depth,
                      void *argument, UBaseType_t priority, TaskHandle_t *handle) {
    (void)name;
    (void)depth;
    (void)argument;
    (void)priority;
    (void)handle;
    draw_task = function;
    return pdPASS;
}

/** 功能：创建绘图队列；参数：count，容量；size，消息长度；返回：队列。 */
QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size) {
    assert(count == 16U && size <= sizeof queued);
    queued_size = size;
    return queued;
}

/** 功能：接受生产者绘图请求但不替消费者报告进展；参数：同 FreeRTOS；返回：成功。 */
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    assert(queue == queued && wait == portMAX_DELAY && !queued_full);
    memcpy(queued, item, queued_size);
    queued_full = true;
    return pdPASS;
}

/** 功能：模拟有界空闲与真实消息消费；参数：同 FreeRTOS；返回：成功或超时。 */
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    assert(queue == queued && wait == pdMS_TO_TICKS(250U));
    if (queued_full) {
        memcpy(item, queued, queued_size);
        queued_full = false;
        return pdPASS;
    }
    if (idle_once) {
        idle_once = false;
        return pdFAIL;
    }
    longjmp(stopped, 1);
}

/** 功能：删除队列；参数：queue，句柄；返回：无。 */
void vQueueDelete(QueueHandle_t queue) {
    assert(queue == queued);
}

/** 功能：为文本所有权提供真实分配；参数：size，长度；返回：内存。 */
void *pvPortMalloc(size_t size) {
    return malloc(size);
}

/** 功能：释放文本副本；参数：pointer，指针；返回：无。 */
void vPortFree(void *pointer) {
    free(pointer);
}

/** 功能：禁止忽略初始化错误；参数：无；返回：无。 */
void Error_Handler(void) {
    assert(false);
}

/** 功能：模拟屏幕已经初始化；参数：无；返回：无。 */
void st7789_init(void) {
}

/** 功能：模拟绘制完成或停滞，停滞时禁止提前报告心跳；参数：同屏幕接口；返回：无。 */
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
    if (blocked_draw) {
        longjmp(stopped, 1);
    }
    ++renders;
}

/** 功能：提供未使用文本绘制边界；参数：同屏幕接口；返回：无。 */
void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color,
                         uint16_t background, const font_t *font) {
    (void)x;
    (void)y;
    (void)str;
    (void)color;
    (void)background;
    (void)font;
}

/** 功能：提供未使用图片绘制边界；参数：同屏幕接口；返回：无。 */
void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image) {
    (void)x;
    (void)y;
    (void)image;
}

/** 功能：验证消费者创建、排队、实际绘制、空闲和卡住的心跳语义；参数：无；返回：通过返回零。 */
int main(void) {
    ui_init();
    assert(draw_task != NULL && pulses == 0U);
    ui_fill_color(0U, 0U, 1U, 1U, 0U);
    assert(pulses == 0U);
    run_ui();
    assert(renders == 1U && pulses == 1U);
    idle_once = true;
    run_ui();
    assert(renders == 1U && pulses == 2U);
    ui_fill_color(0U, 0U, 1U, 1U, 0U);
    blocked_draw = true;
    run_ui();
    assert(renders == 1U && pulses == 2U);
    puts("OTA actual UI progress hooks passed");
    return 0;
}
