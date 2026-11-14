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

/** 标识最近一次安装失败的具体操作阶段。 */
typedef enum {
    OTA_INSTALL_STAGE_NONE = 0,
    OTA_INSTALL_STAGE_AUTHENTICATE,
    OTA_INSTALL_STAGE_PERMIT_JOURNAL,
    OTA_INSTALL_STAGE_ERASE_FOOTER,
    OTA_INSTALL_STAGE_ERASE_FOOTER_READBACK,
    OTA_INSTALL_STAGE_ERASE_APPLICATION,
    OTA_INSTALL_STAGE_FETCH,
    OTA_INSTALL_STAGE_PROGRAM,
    OTA_INSTALL_STAGE_PROGRAM_READBACK,
    OTA_INSTALL_STAGE_IMAGE_READ,
    OTA_INSTALL_STAGE_IMAGE_VECTOR,
    OTA_INSTALL_STAGE_IMAGE_HASH,
    OTA_INSTALL_STAGE_FOOTER_PROGRAM,
    OTA_INSTALL_STAGE_FOOTER_PROGRAM_READBACK,
    OTA_INSTALL_STAGE_FOOTER_COMMIT,
    OTA_INSTALL_STAGE_FOOTER_COMMIT_READBACK,
    OTA_INSTALL_STAGE_FOOTER_READ,
    OTA_INSTALL_STAGE_FOOTER_PARSE,
    OTA_INSTALL_STAGE_TRIAL_JOURNAL
} ota_install_stage_t;

/** 保存最近一次安装调用的故障位置，不保存包内容或密钥。 */
typedef struct {
    ota_install_stage_t stage; // 失败阶段，成功时为 NONE
    uint32_t image_offset; // 镜像正文内偏移，不含包头；正文外操作为零，摘要失败时为已检查长度
    uint32_t address; // Flash 操作起点；摘要阶段为已检查区间末端，不涉及 Flash 时为零
    ota_store_result_t store_result; // 日志阶段的保存结果，其他阶段为 OK
} ota_install_diagnostic_t;

/** 单线程安装器的最近一次诊断快照；每次安装开始覆盖，成功返回时清零，不支持并发安装。 */
ota_install_diagnostic_t ota_install_diagnostic(void);
bool ota_install(const ota_install_io_t *io, const uint8_t header[256], const uint8_t key[32], ota_record_t *record);

#endif
