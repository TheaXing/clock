#ifndef __APP_WORKQUEUE_H__
#define __APP_WORKQUEUE_H__

#include <stdbool.h>

typedef void (*work_t)(void *param);

void workqueue_init(void);
void workqueue_run(work_t work, void *param);
/* 在任务上下文非阻塞提交，队列满时立即返回。 */
bool workqueue_try_run(work_t work, void *param);

#endif // __APP_WORKQUEUE_H__ 头文件保护结束
