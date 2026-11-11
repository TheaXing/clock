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
    
    while (1)
    {
        xQueueReceive(work_msg_queue, &msg, portMAX_DELAY);//从work_msg_queue 队列中接收一条消息，保存到 msg 中
        msg.work(msg.param);//执行工作函数
    }
}

//初始化工作队列
void workqueue_init(void)
{
    work_msg_queue = xQueueCreate(16, sizeof(work_message_t));//创建队列：最多可以放16条消息
    configASSERT(work_msg_queue);
    xTaskCreate(work_func, "workqueue", 1024, NULL, 5, NULL);//创建工作队列任务
}

//将任务提交到消息队列
void workqueue_run(work_t work, void *param)
{
    configASSERT(work_msg_queue);//检查队列是否初始化完成
    work_message_t msg = { work, param };
    xQueueSend(work_msg_queue, &msg, portMAX_DELAY);
}
