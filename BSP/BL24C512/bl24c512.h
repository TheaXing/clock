#ifndef BL24C512_H
#define BL24C512_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BL24C512_CAPACITY 65536UL
#define BL24C512_PAGE_SIZE 128U
/* 启动后由唯一 EEPROM 工作任务独占 I2C1；仅在调度器启动前或普通任务上下文调用，拒绝调度器已暂停的情况。每次 HAL 调用只暂停任务调度，保留中断，并在结束后恢复。100 kHz 下每块 128 字节约需 12 毫秒；总线故障时单次传输可能阻塞任务约 26 毫秒，含两次忙等待的就绪探测最多约 52 毫秒。分块之间及等待写应答时让出处理器。 */
bool bl24c512_read(uint32_t address, void *data, size_t length);
bool bl24c512_write(uint32_t address, const void *data, size_t length);
#endif
