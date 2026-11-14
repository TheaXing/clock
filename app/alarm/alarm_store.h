#ifndef CLOCK_ALARM_STORE_H
#define CLOCK_ALARM_STORE_H
#include <stddef.h>
#include "alarm_types.h"

typedef bool (*alarm_store_read_t)(void *context, uint32_t address, void *data, size_t length);
typedef bool (*alarm_store_write_t)(void *context, uint32_t address, const void *data,
                                    size_t length);
/** 绑定 EEPROM 日志所需的读写回调及其上下文。 */
typedef struct {
    alarm_store_read_t read;   // 读取指定地址字节的回调
    alarm_store_write_t write; // 写入并等待 EEPROM 完成的回调
    void *context;             // 原样传递给读写回调的上下文
} alarm_store_io_t;
typedef enum {
    ALARM_STORE_OK = 0,
    ALARM_STORE_EMPTY,
    ALARM_STORE_IO_ERROR,
    ALARM_STORE_CORRUPT,
    ALARM_STORE_INVALID
} alarm_store_result_t;
/** 保存双槽 EEPROM 日志接口、活动记录及更新基线。 */
typedef struct {
    alarm_store_io_t io;  // 底层 EEPROM 读写接口
    uint32_t generation;  // 持久化记录代数
    int active_slot;      // 活动槽位编号，-1 表示尚无有效记录
    bool update_baseline; // 是否已捕获必须保持一致的严格更新基线
} alarm_store_t;

/* 保留 EEPROM 区域为 [0,2048)，每个槽位 1024 字节；不得写入其他区域。 */
void alarm_store_init(alarm_store_t *store, alarm_store_io_t io);
alarm_store_result_t alarm_store_load(alarm_store_t *store, alarm_config_t *config);
/* 读取、合并、保存流程使用严格读取：两个槽位均须可读，已知代数不得回退。
 * OK 提供配置；EMPTY 或 CORRUPT 保持配置不变，并确认无有效基线。以上三种结果
 * 都会捕获基线，后续保存拒绝期间发生的代数变化。
 * IO_ERROR 或 INVALID 保持配置及存储状态不变；启动加载可回退到其他有效槽位。 */
alarm_store_result_t alarm_store_load_for_update(alarm_store_t *store, alarm_config_t *config);
alarm_store_result_t alarm_store_save(alarm_store_t *store, const alarm_config_t *config);
#endif
