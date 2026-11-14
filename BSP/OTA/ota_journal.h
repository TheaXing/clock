#ifndef OTA_JOURNAL_H
#define OTA_JOURNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_REQUESTED = 1,
    OTA_STATE_INSTALLING = 2,
    OTA_STATE_TRIAL = 3,
    OTA_STATE_CONFIRMED = 4,
    OTA_STATE_FAILED = 5
} ota_state_t;

/** 保存一次已经提交的升级事务状态。 */
typedef struct {
    uint32_t generation; // 持久化记录代数，保存时由日志模块派生
    ota_state_t state; // 当前升级状态
    uint32_t attempts; // 未确认试启动次数
    uint32_t error; // 最近错误编号
    uint8_t manifest[256]; // 目标包头，尚未获取时全零
} ota_record_t;

/** 提供受调用者事务锁保护的 EEPROM 字节访问。 */
typedef struct {
    bool (*read)(void *, uint32_t, void *, size_t); // 按地址完整读取，失败返回假
    bool (*write)(void *, uint32_t, const void *, size_t); // 单页完整写入，失败返回假
    void *context; // 传递给存储回调的上下文
} ota_journal_io_t;

typedef enum {
    OTA_STORE_OK,
    OTA_STORE_EMPTY,
    OTA_STORE_CORRUPT,
    OTA_STORE_IO,
    OTA_STORE_INVALID
} ota_store_result_t;

ota_store_result_t ota_journal_load(const ota_journal_io_t *io, ota_record_t *out);
ota_store_result_t ota_journal_save(const ota_journal_io_t *io, ota_record_t *record);

#endif
