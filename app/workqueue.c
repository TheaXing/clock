#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "workqueue.h"

/** 保存待执行的工作函数及其上下文参数。 */
typedef struct {
    work_t work; // 由工作队列任务调用的处理函数
    void *param; // 传递给处理函数的上下文参数
} work_message_t;

static QueueHandle_t work_msg_queue; //全局队列句柄（这个队列用于保存别人提交过来的工作消息）

/*工作队列任务函数*/
/**
 * 功能：从工作队列接收消息并在任务上下文调用其工作函数。
 * 参数：param，任务启动参数，未使用。
 * 返回：无，任务持续运行。
 */
static void work_func(void *param) {
    work_message_t msg;
    (void)param;

    while (1) {
        if (xQueueReceive(work_msg_queue, &msg, portMAX_DELAY) == pdPASS && msg.work) {
            msg.work(msg.param);
        }
    }
}

//初始化工作队列
/**
 * 功能：创建工作消息队列和专用工作任务。
 * 参数：无。
 * 返回：无。
 */
void workqueue_init(void) {
    work_msg_queue = xQueueCreate(16, sizeof(work_message_t)); //创建队列：最多可以放16条消息
    configASSERT(work_msg_queue);
    BaseType_t created = xTaskCreate(work_func, "workqueue", 1024, NULL, 5, NULL);
    configASSERT(created == pdPASS);
}

//将任务提交到消息队列
/**
 * 功能：阻塞提交工作消息，直到队列有空位。
 * 参数：work，待调用的工作函数；param，传递给工作函数的上下文。
 * 返回：无。
 */
void workqueue_run(work_t work, void *param) {
    configASSERT(work_msg_queue); //检查队列是否初始化完成
    configASSERT(work != NULL);
    work_message_t msg = {work, param};
    xQueueSend(work_msg_queue, &msg, portMAX_DELAY);
}

/**
 * 功能：以非阻塞方式提交工作消息，队列满时立即失败。
 * 参数：work，待调用的工作函数；param，传递给工作函数的上下文。
 * 返回：提交成功返回 true，参数无效、队列未初始化或已满返回 false。
 */
bool workqueue_try_run(work_t work, void *param) {
    if (!work_msg_queue || !work) {
        return false;
    }
    work_message_t msg = {work, param};
    return xQueueSend(work_msg_queue, &msg, 0) == pdPASS;
}
