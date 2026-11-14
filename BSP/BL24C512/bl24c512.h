#ifndef BL24C512_H
#define BL24C512_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BL24C512_CAPACITY 65536UL
#define BL24C512_PAGE_SIZE 128U
/* 初始化必须在调度器启动前完成。事务从日志扫描一直覆盖最终提交复查。
 * 持有此锁时不得等待闹钟所有者或网络操作；仅允许普通任务上下文等锁。 */
bool bl24c512_transaction_init(void);
bool bl24c512_transaction_begin(void);
void bl24c512_transaction_end(void);
/* OTA 构建由事务锁保护完整业务操作。每次 HAL 调用只暂停任务调度，保留中断，
 * 并在结束后恢复；分块之间及等待写应答时让出处理器。 */
bool bl24c512_read(uint32_t address, void *data, size_t length);
bool bl24c512_write(uint32_t address, const void *data, size_t length);
#endif
