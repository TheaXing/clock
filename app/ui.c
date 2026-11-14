#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "main.h"
#include "st7789.h"
#include "ui.h"
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
#include "ota_service.h"
#define UI_RECEIVE_WAIT pdMS_TO_TICKS(250U)
#else
#define UI_RECEIVE_WAIT portMAX_DELAY
#endif

typedef enum { UI_ACTION_FILL_COLOR, UI_ACTION_WRITE_STRING, UI_ACTION_DRAW_IMAGE } ui_action_t;

/** 描述一条由界面任务串行处理的绘图消息。 */
typedef struct {
    ui_action_t action; // 绘图操作类型
    union {
        /** 保存矩形填色的范围与颜色。 */
        struct {
            uint16_t x1;    // 矩形起点横坐标，单位为像素
            uint16_t y1;    // 矩形起点纵坐标，单位为像素
            uint16_t x2;    // 矩形终点横坐标，单位为像素
            uint16_t y2;    // 矩形终点纵坐标，单位为像素
            uint16_t color; // RGB565 填充颜色
        } fill_color;       // 矩形填色参数
        /** 保存文本绘制的位置、内容和字体资源。 */
        struct {
            uint16_t x;         // 文本起点横坐标，单位为像素
            uint16_t y;         // 文本起点纵坐标，单位为像素
            char *str;          // 入队后由界面任务释放的文本副本
            uint16_t color;     // RGB565 文字颜色
            uint16_t bg_color;  // RGB565 文字背景颜色
            const font_t *font; // 绘制期间持续有效的字体资源
        } write_string;         // 文本绘制参数
        /** 保存图片绘制的位置和图片资源。 */
        struct {
            uint16_t x;           // 图片起点横坐标，单位为像素
            uint16_t y;           // 图片起点纵坐标，单位为像素
            const image_t *image; // 绘制期间持续有效的图片资源
        } draw_image;             // 图片绘制参数
    } data;                       // 与操作类型对应的绘图参数
} ui_message_t;

static QueueHandle_t ui_queue;

/**
 * 功能：从绘图队列取出消息并串行操作显示屏，绘制后释放文本副本。
 * 参数：param，任务启动参数，未使用。
 * 返回：无，任务持续运行。
 */
static void ui_func(void *param) {
    ui_message_t msg;
    (void)param;
    st7789_init();

    for (;;) {
        if (xQueueReceive(ui_queue, &msg, UI_RECEIVE_WAIT) != pdPASS) {
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
            ota_service_heartbeat(OTA_HEARTBEAT_UI);
#endif
            continue;
        }

        switch (msg.action) {
        case UI_ACTION_FILL_COLOR:
            st7789_fill_color(msg.data.fill_color.x1, msg.data.fill_color.y1,
                              msg.data.fill_color.x2, msg.data.fill_color.y2,
                              msg.data.fill_color.color);
            break;
        case UI_ACTION_WRITE_STRING:
            st7789_write_string(msg.data.write_string.x, msg.data.write_string.y,
                                msg.data.write_string.str, msg.data.write_string.color,
                                msg.data.write_string.bg_color, msg.data.write_string.font);
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
#if defined(CLOCK_OTA_ENABLED) && CLOCK_OTA_ENABLED
        ota_service_heartbeat(OTA_HEARTBEAT_UI);
#endif
    }
}

/**
 * 功能：创建绘图队列和界面任务，重复初始化时直接返回。
 * 参数：无。
 * 返回：无。
 */
void ui_init(void) {
    if (ui_queue != NULL) {
        return;
    }

    ui_queue = xQueueCreate(16, sizeof(ui_message_t));
    if (ui_queue == NULL) {
        Error_Handler();
        return;
    }

    if (xTaskCreate(ui_func, "ui", 1024, NULL, 8, NULL) != pdPASS) {
        vQueueDelete(ui_queue);
        ui_queue = NULL;
        Error_Handler();
    }
}

/* 生产者运行在任务上下文，阻塞提交可保证页面重绘时不丢失填色、文字或图片。
 * 禁止在中断、定时器服务回调或绘图任务自身调用。 */
/**
 * 功能：在任务上下文中阻塞提交绘图消息，保证页面绘制顺序。
 * 参数：msg，待复制到队列的绘图消息；禁止在中断、定时器回调或绘图任务中调用。
 * 返回：入队成功返回 pdPASS，队列未初始化或提交失败返回 pdFAIL。
 */
static BaseType_t ui_submit(const ui_message_t *msg) {
    if (ui_queue == NULL) {
        return pdFAIL;
    }
    return xQueueSend(ui_queue, msg, portMAX_DELAY);
}

/**
 * 功能：向绘图队列提交矩形填色请求，坐标包含两个端点。
 * 参数：x1、y1，起点坐标；x2、y2，终点坐标；color，RGB565 填充颜色。
 * 返回：无。
 */
void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    ui_message_t msg = {0};
    msg.action = UI_ACTION_FILL_COLOR;
    msg.data.fill_color.x1 = x1;
    msg.data.fill_color.y1 = y1;
    msg.data.fill_color.x2 = x2;
    msg.data.fill_color.y2 = y2;
    msg.data.fill_color.color = color;
    if (ui_submit(&msg) != pdPASS) {
        Error_Handler();
    }
}

/**
 * 功能：复制文本并提交绘制请求，入队成功后由界面任务释放副本。
 * 参数：x、y，起始像素坐标；str，待显示文本；color，文字颜色；bg_color，背景颜色；font，绘制完成前持续有效的字体资源。
 * 返回：无。
 */
void ui_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color,
                     const font_t *font) {
    if (str == NULL || font == NULL) {
        return;
    }

    size_t length = strlen(str) + 1;
    char *copy = pvPortMalloc(length);
    if (copy == NULL) {
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

    /* 只有成功入队后，文本副本的所有权才交给绘图任务。 */
    if (ui_submit(&msg) != pdPASS) {
        vPortFree(copy);
        Error_Handler();
    }
}

/**
 * 功能：提交图片绘制请求，图片描述符及数据须持续有效直至绘制完成。
 * 参数：x、y，起始像素坐标；image，待绘制图片描述符。
 * 返回：无。
 */
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image) {
    if (image == NULL) {
        return;
    }

    ui_message_t msg = {0};
    msg.action = UI_ACTION_DRAW_IMAGE;
    msg.data.draw_image.x = x;
    msg.data.draw_image.y = y;
    msg.data.draw_image.image = image;
    if (ui_submit(&msg) != pdPASS) {
        Error_Handler();
    }
}
