#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "workqueue.h"

typedef struct
{
    work_t work;
    void *param;
} work_message_t;//消息结构体

static QueueHandle_t work_msg_queue;//全局队列句柄（这个队列用于保存别人提交过来的工作消息）

/*工作队列任务函数*/
static void work_func(void *param)
{
    work_message_t msg;
    (void)param;
    
    while (1)
    {
        if (xQueueReceive(work_msg_queue, &msg, portMAX_DELAY) == pdPASS && msg.work)
            msg.work(msg.param);
    }
}

//初始化工作队列
void workqueue_init(void)
{
    work_msg_queue = xQueueCreate(16, sizeof(work_message_t));//创建队列：最多可以放16条消息
    configASSERT(work_msg_queue);
    BaseType_t created = xTaskCreate(work_func, "workqueue", 1024, NULL, 5, NULL);
    configASSERT(created == pdPASS);
}

//将任务提交到消息队列
void workqueue_run(work_t work, void *param)
{
    configASSERT(work_msg_queue);//检查队列是否初始化完成
    configASSERT(work != NULL);
    work_message_t msg = { work, param };
    xQueueSend(work_msg_queue, &msg, portMAX_DELAY);
}

bool workqueue_try_run(work_t work, void *param)
{
    if (!work_msg_queue || !work) return false;
    work_message_t msg = {work, param};
    return xQueueSend(work_msg_queue, &msg, 0) == pdPASS;
}
