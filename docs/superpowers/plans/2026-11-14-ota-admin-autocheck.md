# 局域网 OTA 后台与开机自动升级 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 交付可上传/发布认证固件的本地后台、一条命令构建打包，以及安全的开机自动更新。

**Architecture:** Node无依赖后台保存不可变版本并原子发布当前指针；App在唯一网络任务中健康确认后发现版本，将固定候选写入既有日志；Boot重验认证和版本并从不可变URL安装。保留原人工升级及普通独立构建。

**Tech Stack:** C11、STM32F4 HAL/FreeRTOS、Node20+内置库、原生HTML/CSS/JS、PowerShell/CMake、MSVC。

**Spec:** `docs/superpowers/specs/2026-11-14-ota-admin-autocheck-design.md`，用户已明确确认并授权实现、测试及编译。

## Global Constraints

- 不增加硬件、不烧录、不修改选项字节、不公开部署、不接入真实云端。
- 复用原256B认证包、Flash布局、EEPROM v1；发布密钥仅在Boot，App只保留管理令牌摘要。
- 默认路径 `/firmware.ota`，`?latest=1`发现，`?id=<32个小写hex>`固定对象；自动基础路径最多156B。
- 最新JSON：无发布`{"available":false}`；有发布`{"available":true,"version":正u32,"packageId":"32hex","header":"512hex"}`，不超过1024B。
- 自动最多3次网络检查：首次立即，失败后分别5s/15s；无新版本不写EEPROM；健康、维护、日志条件须在worker再次验证。
- Boot自动非零REQUESTED须HMAC通过且比有效旧版新，INSTALLING固定原目标；保持三次TRIAL及计数写失败锁存保护。
- 后台上传只接受有效`.ota`，最大458752B，2个并发上传、30s期限，默认256MiB配额、最多4包缓存；不自动删除历史。
- 所有新增C注释中文、函数和结构体说明/成员行尾注释，遵守AGENTS.md和docs/CODING_STYLE.md。

## Review Focus

- 发布切换或重启不能改变已选目标，不能用新字节冒充旧ETag。
- 自动检查不可覆盖TRIAL/活动人工请求，也不可在软件回退后反复重启。
- 上传截断、磁盘满或原子发布失败不能改变当前发布。
- 新脚本不能继承旧测试配置、生成新密钥或留下半成品供误上传。
- 本地网页须真实上传/发布验收，不能用页面模拟结果代替后台行为。

## 固定任务边界与接口

后台 `openReleaseStore({directory,key,maxBytes})` 返回异步存储对象，提供 `list()`→`{releases,current,storage:{usedBytes,limitBytes}}`、`upload(Buffer)`→`{release,created}`、`publish(packageId)`→release、`read(packageId)`→`{bytes,release}`、`latest()`→上述发现JSON。release公开字段至少`packageId/version/imageSize/packageSize/packageSha256/uploadedAt/board`，当前发布额外提供publishedAt。后台可按具体正确性需要微调内部函数，HTTP字段保持上述契约。

`createAdminServer({store,adminTokenHex,path})` 提供设计的HTTP API和固定静态资源。CLI `ota.mjs admin --key-file FILE --data-dir DIR --host ADDRESS --port PORT --path PATH`；保留init/pack/serve，serve也支持匹配当前单包的latest/id查询，供新版Boot兼容人工工具。

构建脚本 `scripts/build_ota.ps1 -Version <u32> -Configuration Debug|Release [-OtaConfig FILE] [-KeyFile FILE] [-OutputDirectory DIR] [-ActiveBuzzer] [-Factory]`；默认配置/JSON为app实际文件，输出默认`build/ota-packages/v<Version>`。生成配置副本覆盖版本，不修改用户源配置；Node辅助`tools/ota/build-support.mjs`由脚本任务独占。

启动脚本 `scripts/start_ota_server.ps1 [-KeyFile FILE] [-DataDirectory DIR] [-BindAddress 0.0.0.0] [-Port 8788] [-PackagePath /firmware.ota]`；默认数据在`.ota-data`。不初始化/覆盖密钥。

设备新增纯解析 `BSP/OTA/ota_discovery.[ch]`：`ota_hint_t {bool available; ota_manifest_t manifest;}`，`bool ota_discovery_parse(const char *json,ota_hint_t *)`；严格验证发现JSON、候选字段与头一致、最大1024B。新增明确弱接口`ota_manifest_read_hint(const uint8_t header[256],ota_manifest_t*)`只检查结构/公开身份，不认证，不能授权安装；原trusted/认证API契约保持。

应用新增 `void ota_service_autocheck_poll(bool connected,uint32_t now_ms)`，仅网络任务调用；现有network_poll保留延迟复位/确认。AUTO默认OTA=1，普通构建=0。硬件复位原因快照在初始化时读取并清理RCC标志；可在独立适配层实现以便桩测试，不读取/输出真实秘密。

自动存储job必须携带完整候选头，不用全局可变网络缓冲作排队指针。仅CONFIRMED匹配当前包时可提交；物理正常启动对“明确预检失败的旧自动REQUESTED”允许重新检查/重试，软件回退启动跳过。manual REQUESTED保持全零。Boot错误1为预检失败、2为安装失败，仍保持兼容。

Boot纯模块 `bootloader/boot_target.[ch]` 由主执行者定义/测试目标选择、HMAC/版本/身份匹配、路径派生；只依赖现有纯OTA协议，无新持久格式。原始KEY1强制可跳过历史固定目标，从当前发布恢复。第一次从别名得到认证头后，后续正文也改用同包`?id=`；INSTALLING重试不能重新选latest。

## Task 1: 后台存储、API与网页

**Files:** `tools/ota/release-store.mjs`、`admin-server.mjs`、`admin/*`、`test_admin.mjs`、现有`ota.mjs/server.mjs`和工具README。

- [x] 先写认证/坏包/超限/中断、版本冲突、原子发布故障与重启、不可变Range和身份切换行为测试并观察RED。
- [x] 实现有界持久化存储及管理API、静态白名单与认证、latest/id/兼容别名。
- [x] 实现真实页面令牌登录、上传进度、版本列表、发布确认、错误反馈及窄屏布局。
- [x] Node测试和原有17项回归通过，写任务证据报告。

## Task 2: 构建打包与后台启动脚本

**Files:** `scripts/build_ota.ps1`、`start_ota_server.ps1`、`tools/ota/build-support.mjs`、`test_build_support.mjs`及脚本测试。

- [x] 先证明缺配置、密钥不一致、版本错误、缓存绕过、输出冲突与失败发布保护的RED。
- [x] 实现配置副本/版本统一、显式路径、硬件选项透传、实际秘密扫描、原子交付打包、可选Factory。
- [x] 用fixture/受控编译适配验证行为，主执行者随后用真实ARM编译验收。

## Task 3: App开机发现及自动请求

**Files:** `app/ota/*`、`app/app.c`、必要的Core初始化USER CODE、`BSP/OTA/ota_discovery.*`、`ota_image.*`、`ota_config.h`、跟踪新测试与现有App测试。

- [x] RED覆盖发现JSON、同旧版本、无发布、健康/维护/存储准入、ACK丢失、有限网络重试、无更新零写、物理/软件失败回退。
- [x] 实现唯一网络任务发现与固定候选排队，worker重新验证，当前升级请求状态和自动状态分离。
- [x] 保留原28服务场景、三心跳、TRIAL计数和既有人工升级，普通构建提供禁用实现。

## Task 4: Boot目标固定与总集成

**Files:** `bootloader/boot_target.*`、`main.c`、必要的net小改、`BSP/OTA/tests/test_boot_target.c/run_boot_target_tests.ps1`、CMake/根测试入口/忽略规则。

- [x] 先写真实认证fixture下目标选择/错误HMAC/旧版/切版/INSTALLING/强制KEY1与路径边界RED。
- [x] 实现目标派生与选择，main接入HMAC、精确包头和不可变正文，保留原全部安装安全约束。
- [x] 全部跟踪及当前遗留测试、普通/OTA Debug/Release、实际构建脚本与产物检查。

## Task 5: 浏览器验收、审阅与交付

- [x] 启动隔离数据/公开测试凭据的真实后台，浏览器验证上传/发布/刷新/错误和窄屏，保存截图。
- [x] 新鲜整体独立审阅，重要问题以回归修复，记录限制和未上板项。
- [x] 更新设计/中文操作说明与执行账本，提交代码，保留当前功能分支；不推送、不烧录。

## 执行约定

- 用户已确认本设计并要求完成全部实现/测试/编译，不重复请求设计或计划批准。
- 在现有`codex/migrate-weather-clock`及指定工作区实施；独立文件范围并行，主执行者负责Boot、总集成和浏览器验收。
- 所有代理不得提交共享分支、不得改其他任务所有文件；接口冲突及时报告。
