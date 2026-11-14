#ifndef BL24C512_H
#define BL24C512_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BL24C512_CAPACITY 65536UL
#define BL24C512_PAGE_SIZE 128U

typedef enum {
    BL24C512_OPERATION_NONE = 0,
    BL24C512_OPERATION_READ,
    BL24C512_OPERATION_WRITE,
    BL24C512_OPERATION_READY
} bl24c512_operation_t;

/** 保存最近一次失败传输的元数据，不包含 EEPROM 内容。 */
typedef struct {
    bool valid;          // 是否已记录失败传输
    uint8_t operation;   // 失败操作，取值为 bl24c512_operation_t
    uint32_t address;    // 失败分块或应答探测关联写页的起始字节地址
    uint16_t length;     // 失败分块或关联写页的字节数
    uint32_t hal_status; // HAL 调用返回的状态码
    uint32_t hal_error;  // HAL 在失败时保存的错误码
    uint32_t hal_state;  // HAL 在失败时保存的状态值
} bl24c512_diagnostic_t;

/* 取得最近失败快照；成功传输不清除记录，正常写周期内的暂时非应答不发布。
 * out 为空时不执行操作。 */
void bl24c512_get_diagnostic(bl24c512_diagnostic_t *out);
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
