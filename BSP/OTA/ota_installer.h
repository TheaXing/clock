#ifndef OTA_INSTALLER_H
#define OTA_INSTALLER_H

#include "ota_journal.h"

/** 提供安装事务所需的日志、Flash 和已关闭连接的包下载访问。 */
typedef struct {
    ota_journal_io_t journal; // 与应用共用格式的持久化日志访问
    void *context; // Flash 和下载回调上下文
    bool (*erase_sector)(void *, unsigned); // 仅允许擦除应用扇区四至七
    bool (*program)(void *, uint32_t, const void *, size_t); // 按四字节对齐编程并报告结果
    bool (*read_flash)(void *, uint32_t, void *, size_t); // 读取内部 Flash 最新真实字节，适配层负责清理擦写后的缓存
    bool (*fetch)(void *, uint32_t, void *, size_t); // 下载完整包偏移对应数据，单次最多四千零九十六字节
} ota_install_io_t;

bool ota_install(const ota_install_io_t *io, const uint8_t header[256], const uint8_t key[32], ota_record_t *record);

#endif
