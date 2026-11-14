# 单应用分区 OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 在现有 STM32F407VE/ESP32-C3/BL24C512 上完成经过认证的单槽无线升级与中断恢复。

**Architecture:** 纯 C 公共模块定义包头、完整镜像校验、双槽 EEPROM 日志和安装事务；裸机 Bootloader 独立控制 AT 被动接收并执行安装。应用通过维护锁与异步存储任务保存请求、延迟复位和确认试启动，电脑端生成认证包并通过严格 Range 服务发布。

**Tech Stack:** C11、STM32F4 HAL/CMSIS、FreeRTOS、ARM GCC/CMake、MSVC、Node.js 20+ 内置库。

**Spec:** `docs/superpowers/specs/2026-11-14-single-slot-ota-design.md`（用户已确认）。

## Global Constraints

- 不增加硬件、不自动烧录、不修改 MCU 选项字节、不操作云端。
- Boot `[0x08000000,0x08010000)`；App `[0x08010000,0x0807FF00)`；footer `[0x0807FF00,0x08080000)`，payload 最大458496。
- 包头256字节，认证/序列化严格遵循设计 §5；EEPROM 两槽0x0800/0x0C00、1024字节，384字节体，提交页偏移0x380。
- 首次擦除扇区7，再4/5/6；日志 INSTALLING 成功后才擦除；完整 Flash 摘要成功后才提交 footer。
- 不使用外部固件缓存、不拼接不同包、不宣称本地回滚或断点续传。
- 所有手写 C 使用中文函数/结构体/成员注释，遵守 `docs/CODING_STYLE.md`。
- 可复现新测试位于 `BSP/OTA/tests/` 和 `tools/ota/`；生产凭据全部 Git 忽略，不写入日志。

## Review Focus

- EEPROM 最后提交应答丢失：不把未确认写入当作擦除许可。
- 部分 Flash 或旧日志回退：完整 footer/摘要/精确向量验证必须阻止启动半包。
- AT 输入窗、异步消息与二进制载荷：载荷不能被 OK/ERROR/NUL 截断。
- HTTP 身份在分块之间改变：Range/总长/ETag/If-Match 不一致必须中止。
- 闹钟本地编辑/云端提交与 OTA 并发：维护准入与完整 EEPROM 事务互斥，失败后恢复服务。

## 固定公共接口

`BSP/OTA/ota_crypto.h`：`ota_sha256_t` 流式上下文；`ota_sha256_init/update/final`；`ota_sha256(const void *,size_t,uint8_t[32])`；`ota_hmac_sha256(const uint8_t key[32],const void *,size_t,uint8_t[32])`；`ota_constant_equal(const void *,const void *,size_t)`。

`BSP/OTA/ota_image.h`：宏 `OTA_APP_BASE`=0x08010000u、`OTA_FOOTER_BASE`=0x0807FF00u、`OTA_IMAGE_MAX`=458496u、`OTA_HEADER_SIZE`=256u、`OTA_FOOTER_COMMIT`=0x3141544Fu。
`ota_manifest_t` 包含 `uint32_t image_size, version; uint8_t digest[32], id[16], raw[256];`。
`bool ota_manifest_parse(const uint8_t header[256],const uint8_t key[32],bool installed,ota_manifest_t *out)`；installed 只接受提交标记，非 installed 只接受0xFFFFFFFF。
`bool ota_image_vectors_valid(const uint8_t first[8],uint32_t image_size)`；`bool ota_image_verify(const ota_manifest_t *,const uint8_t *image)`。

`BSP/OTA/ota_journal.h`：`ota_state_t` 顺序值 IDLE=0、REQUESTED=1、INSTALLING=2、TRIAL=3、CONFIRMED=4、FAILED=5。
`ota_record_t` 字段 `uint32_t generation; ota_state_t state; uint32_t attempts,error; uint8_t manifest[256];`。
`ota_journal_io_t` 回调 `bool (*read)(void *,uint32_t,void *,size_t)`、`bool (*write)(void *,uint32_t,const void *,size_t)` 及 `void *context`。
`ota_store_result_t` 为 `OTA_STORE_OK,OTA_STORE_EMPTY,OTA_STORE_CORRUPT,OTA_STORE_IO,OTA_STORE_INVALID`。
`ota_journal_load(const ota_journal_io_t *,ota_record_t *)` 和 `ota_journal_save(const ota_journal_io_t *,ota_record_t *)`；save 严格重扫后派生新代数，只在完全成功时更新输出，锁由调用者完整持有。

`BSP/OTA/ota_installer.h`：`ota_install_io_t` 含 `ota_journal_io_t journal; void *context; bool (*erase_sector)(void *,unsigned); bool (*program)(void *,uint32_t,const void *,size_t); bool (*read_flash)(void *,uint32_t,void *,size_t); bool (*fetch)(void *,uint32_t,void *,size_t);`，fetch 偏移针对完整`.ota`文件（包含256字节头），每次最多4096字节。
`bool ota_install(const ota_install_io_t *,const uint8_t header[256],const uint8_t key[32],ota_record_t *)`；内部提交 INSTALLING、footer失效/扇区顺序、逐块校验、最后从Flash摘要验证、footer最后提交、TRIAL提交。

应用集成固定边界：`app/ota/ota_service.h` 暴露 `void ota_service_init(void)`（调度器启动前）、`void ota_service_start(void)`（本地服务就绪后）、`void ota_service_network_poll(uint32_t now_ms)`（唯一网络任务）、`void ota_service_status(char *,size_t,uint32_t session)`、`int ota_service_request(const char *body,uint32_t session)`（返回HTTP状态码）。非 OTA 构建提供同接口 disabled 实现。核心头路径统一 `BSP/OTA`。

配置统一由 Git 忽略的 `app/ota_config_local.h` 提供：`CLOCK_OTA_HOST` 字符串、`CLOCK_OTA_PORT`整数、`CLOCK_OTA_PATH`字符串、`CLOCK_OTA_KEY_HEX`64个十六进制字符、`CLOCK_OTA_ADMIN_TOKEN_HEX`64个十六进制字符或空、`CLOCK_OTA_VERSION`正u32。公共 `BSP/OTA/ota_config.h` 由主执行者维护，宏 `CLOCK_OTA_ENABLED` 默认为0；工具初始化同时生成该头和忽略的JSON密钥文件。

## Task 1: 公共协议与事务安装

**Files:** 创建 `BSP/OTA/ota_crypto.[ch]`、`ota_image.[ch]`、`ota_journal.[ch]`、`ota_installer.[ch]`、`tests/test_ota.c`、`tests/run_tests.ps1`。

- [x] 先写标准 SHA/HMAC、独立包 fixture、状态日志和模拟Flash安装故障测试，观察缺失实现或错误实现导致 RED。
- [x] 实现上述固定接口；Flash/EEPROM边界故障注入覆盖提交丢失、扇区7先失效、最后footer提交。
- [x] MSVC `/utf-8 /W4 /WX` 测试 GREEN，包含标准向量和跨语言fixture。

## Task 2: 电脑端 OTA 工具

**Files:** 新建 `tools/ota/ota.mjs`、`protocol.mjs`、`server.mjs`、`test_ota.mjs`、README；不更改现有 OneNET工具。

- [x] 先写行为测试：认证包、错误向量/密钥、工厂HEX、Range/ETag/If-Match/路径固定/冻结快照，观察 RED。
- [x] 实现 Node内置库命令 `init`、`pack`、`serve`，协议与C接口共享精确字段；工厂HEX可选Boot BIN+App BIN+footer，生成发布报告。
- [x] `node --test tools/ota/test_ota.mjs` 全通过；CLI参数失败返回非零且不泄露秘密。

## Task 3: 应用 OTA 与 EEPROM 维护准入

**Files:** `BSP/BL24C512/*`、`app/alarm/alarm_service.[ch]`、`app/ota/ota_service.[ch]`、`app/web/clock_web.c`、`app/web/web_page.c`、`app/app.c`、`Core/Src/main.c`、新跟踪主机测试。

- [x] 先覆盖未启用、错误token/session、异步请求、保存失败、忙/并发、确认包身份、延迟复位的行为测试并观察 RED。
- [x] 全设备事务锁在调度器启动前创建；闹钟load/save事务持锁，OTA worker共用，maintenance由闹钟owner准入。
- [x] GET/POST `/api/ota`、网页UI、2秒宽限复位；10秒健康确认只修改匹配的TRIAL。
- [x] 唯一网络任务调用poll，禁止第二个应用AT所有者；原配置和本地闹钟回归。

## Task 4: 裸机 Bootloader 与构建

**Files:** `bootloader/` 独立主程序/硬件/网络，`BSP/OTA/ota_http.[ch]`，独立链接脚本，根CMake、toolchain、presets、build脚本，`BSP/OTA/ota_config.h`。

- [x] 先写严格HTTP增量解析及AT模拟测试，观察 RED，再实现字节长度驱动状态机。
- [x] 实现带超时UART/ESP初始化、Range获取、固定主机，读完并断开连接才允许Flash写入。
- [x] 实现Boot启动决策、KEY1、EEPROM适配、扇区边界硬限制、安全汇编交接及重试。
- [x] 普通Debug/Release保留原布局；OtaDebug/OtaRelease要求本地配置；Boot固定Os且链接上限64KiB；App footer空间保留。

## Task 5: 总体验证与交付

- [x] 修复现有测试启动的UTF-8输入/GBK执行字节冲突，保持旧显示字节语义。
- [x] 跟踪BSP测试、新OTA/工具测试和当前全套主机测试；完整构建四种App和Boot、RGB/蜂鸣器必要冒烟。
- [x] 用生成的测试凭据验证打包、工厂HEX及ELF地址；测试配置不替换已有生产凭据。
- [x] 独立审阅完整差异，重要问题先复现后修复，确认风险与实板待验项。
- [x] 更新设计状态与中文 `docs/OTA_TESTS.md`，交付代码和产物路径；不推送、不合并、不烧录。

## 执行记录

- 用户已明确确认设计并要求完成实现、测试及构建；实施计划属于该授权，不再重复请求批准。
- 在用户指定的现有工作区和既有 `codex/migrate-weather-clock` 功能分支实施，不移动工作区或创建新工作树。
- 采用独立文件范围并行实施协议/工具/应用，主执行者负责Boot与构建，最后统一审阅；代理不得改写其他任务文件或提交共享分支。
