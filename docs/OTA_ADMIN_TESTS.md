# 局域网 OTA 后台与开机升级操作

本功能使用现有 STM32F407、ESP32-C3 和 EEPROM，不增加硬件。后台运行在电脑上，需要 Node.js 20+；固件编译使用项目原有 ARM GCC/CMake/Ninja。后台无需安装 npm 依赖。

## 一次性配置

1. 电脑和时钟连接同一局域网，为电脑分配固定 IPv4 地址或 DHCP 地址保留。以下 `192.168.1.10` 必须换成电脑实际局域网地址；不能填写 `127.0.0.1` 或 `0.0.0.0`。
2. 检查 `app/clock_config_local.h` 中的 Wi-Fi 配置。在项目根目录初始化一次：

```powershell
node tools/ota/ota.mjs init --config app/ota_config_local.h --key-file app/ota_key_local.json --host 192.168.1.10 --port 8788 --path /firmware.ota --version 1
```

已有配置时不要重新初始化。私下备份配置头和密钥 JSON，后续编译、后台与已装 Bootloader 必须使用同一发布密钥。`adminTokenHex` 用于管理网页登录，`keyHex` 用于签名，两者不可互换。不要上传或提交这两份文件。

下载基础路径最多 156 个 ASCII 字节，完整发现地址 `http://主机:端口/路径?latest=1` 最多 224 字节，以符合现有 ESP-AT 命令上限。初始化、构建和设备端均检查该组合限制；通常的局域网 IPv4 地址配合 `/firmware.ota` 无需调整。

后台地址、端口、下载路径和 Wi-Fi 是 Bootloader 的编译配置。后续只上传 App 无法修改已安装 Bootloader 的配置；改变这些值需要重新安装配套 Bootloader。

## 编译和打包

首次准备工厂镜像：

```powershell
.\scripts\build_ota.ps1 -Version 1 -Configuration Release -Factory
```

输出位于 `build/ota-packages/v1/`：

| 文件 | 用途 |
| --- | --- |
| `clock-v1.ota` | 后台上传的认证升级包 |
| `clock-v1-report.json` | 版本、长度、摘要等构建报告 |
| `clock-factory-v1.hex` | Bootloader、App 和认证描述合并的首次安装镜像 |

脚本从实际配置生成临时副本，仅覆盖本次版本，不改原文件；显式传入配置，核对签名密钥与令牌摘要，检查最终 App 不包含发布密钥或原始管理令牌，成功后才交付整个输出目录。已有输出目录会拒绝覆盖。版本必须是 `1..4294967295` 的整数，日常自动升级必须递增。脚本在整个编译打包期间持有工作区锁，同时启动另一份会明确报错，进程结束或异常退出后由系统释放；不要同时用底层 `build.ps1` 修改同一个构建目录。

主动蜂鸣器板使用 `-ActiveBuzzer`，以后构建都应保持该选项一致；默认仍为原 PWM 蜂鸣器。自定义私有配置使用 `-OtaConfig <头文件> -KeyFile <JSON>`，自定义目录用 `-OutputDirectory <新目录>`。

本次实现不执行烧录。将来首次使用本功能，需要操作者通过已有 SWD 工具安装新版工厂 HEX 一次，具体见 [原 OTA 使用说明](OTA_TESTS.md)。旧版 Bootloader 不能通过仅更新 App 获得新的固定目标下载能力。

## 启动后台

```powershell
.\scripts\start_ota_server.ps1
```

浏览器访问 `http://127.0.0.1:8788/`，同局域网其他电脑使用 `http://192.168.1.10:8788/`。默认监听 `0.0.0.0:8788`，数据保存在项目根目录 `.ota-data/`，此目录已被 Git 忽略。不要删除历史包；中断升级的设备可能仍需下载原目标。

仅允许 Windows 防火墙的专用局域网配置访问 TCP 8788，确保路由器没有启用 AP 客户端隔离。电脑需保持服务运行、网络在线且不休眠。这是受信任局域网的 HTTP 服务，暂不公开到互联网。

可以使用 `-KeyFile <JSON> -DataDirectory <持久目录> -BindAddress <地址> -Port <端口> -PackagePath <下载路径>`。更换监听端口或下载路径时，必须与设备 Bootloader 配置保持一致。`Ctrl+C` 停止服务，重新启动会恢复已上传版本和当前发布。

## 每次发布新版本

```powershell
.\scripts\build_ota.ps1 -Version 2 -Configuration Release
```

1. 打开后台，输入密钥 JSON 中的 `adminTokenHex` 登录。令牌仅保存在当前页面内存，刷新需要重新输入。
2. 上传 `build/ota-packages/v2/clock-v2.ota`。只接受认证 `.ota`，不要上传 BIN、HEX、配置或密钥。
3. 确认列表中的版本和包身份，点击发布并确认。上传仅暂存，发布后设备才能发现。
4. 给时钟正常重新上电或按 RESET。App 先启动本地时钟，健康确认且联网后查询一次最新版本，失败时再有限重试两次。
5. 新版本会排队写入 EEPROM，约两秒后重启进入 Boot。串口观察下载、验证、试启动和 App 健康确认。
6. 设备重新上线后，打开原时钟网页检查实际运行版本和自动检查状态，等待本地服务稳定十秒，并验证闹钟仍保留。`CONFIRMED` 是 EEPROM 内部健康状态，网页中的请求 `state` 不是该状态；正式验收时结合串口和日志读回验证。后台发布成功只表示服务器已发布，不能代表设备已安装。

自动升级只接受比当前运行版本更大的版本号。同版本重复上传相同包会去重；同版本不同内容会被拒绝，应重新编译成更高版本。后台可显式重新发布旧包供人工恢复，但正常 App 不会自动降级。设备在响铃、编辑或存储繁忙时会保留候选并等到允许升级时提交。

默认包存储配额为256 MiB，无自动删除接口。HTTP 上传会预留完整请求长度；容量已满时，重复上传也可能返回507。历史包仍可下载，当前发布不变；需要扩容时通过直接 Node 命令的 `--max-bytes` 配置更大配额。

## 首次完整验收清单

以下需要将来在开发板上执行，本次没有烧录或实板验收：

| 步骤 | 预期结果 |
| --- | --- |
| 安装工厂 v1，上电等待 | Boot 完整认证，进入时钟；本地任务稳定十秒后确认 |
| 后台只上传 v2，不发布 | 开机仍运行 v1，无自动升级请求 |
| 发布 v2，设备正常重启 | 发现 v2，保存固定包头，重启下载，运行 v2 并确认 |
| v2 再次重启 | 不请求升级，不为无更新检查写 EEPROM |
| 关闭后台后重启正常设备 | 时钟/闹钟正常启动，检查有限失败后结束 |
| 下载 v2 时发布 v3 | 当前安装继续固定 v2，不混合两包；以后正常开机再发现 v3 |
| 保存请求后关闭后台 | 预检失败且旧 App 完整时返回旧 App，保留请求；软件回退不产生自动重启循环 |
| 下载中断电后恢复服务 | Boot 只重下已保存的固定目标，不启动半份 App |
| 发布旧版本 | 已确认的新 App 不会自动降级；KEY1 人工恢复仍可显式使用 |

单应用分区没有本地旧版回滚。开始擦写后，必须恢复服务器和网络才能恢复下载。三次未确认试启动后不自动反复安装同一个包；可发布不同认证包或按原说明使用 KEY1 强制恢复。详见 [恢复机制和硬件边界](OTA_TESTS.md)。

## 主机验证

```powershell
.\scripts\test.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
.\scripts\build_ota.ps1 -Version 3 -Configuration Debug
.\scripts\build_ota.ps1 -Version 4 -Configuration Release -Factory
```

测试涵盖存储/认证/上传/发布/重启恢复、Range 与不可变身份、构建脚本失败保护、严格元数据解析、健康确认/日志事务/有限检查与 Boot 固定目标。普通固件仍在原地址独立构建，自动检查禁用。编译、主机测试和浏览器验收不能替代实板断电、AT 串口和任务时序验证。

## 本次验证记录（2026-11-14）

- `scripts/test.ps1 -IncludeLegacy` 完整通过，包含跟踪的 C 测试、50项 Node 测试和当前工作区的补充遗留测试。
- 普通 Debug／Release、OTA Debug／Release 四种真实 ARM 构建通过；新 `build_ota.ps1` 完成 Debug v4 与 Release v5 打包，Release 包同时生成工厂 HEX。
- 最终 OTA Debug App 为328284字节，Release App为272540字节，Boot为17604字节；向量、Flash容量、签名密钥隔离检查通过。
- 浏览器使用公开测试凭据及隔离临时数据目录验证错误令牌、坏包拒绝、上传暂存、确认发布、切换发布、重启保持、刷新重新登录和窄屏布局；验收后已停止测试后台。
- 独立审阅后无重要问题遗留。实际配置入口的224／225字节发现URL、实际生产命令的255／256字节边界及并发构建锁均有回归证据。

上述构建使用被忽略的 `build/ota-validation/` 测试配置与密钥、示例服务器地址，不是开发板的正式部署配置。产物位于 `build/ota-admin-validation/v4/` 和 `v5/`，完整日志位于 `build/ota-admin-final-*.log`。正式使用先完成本文的一次性配置并重新构建。本次没有连接或烧录开发板，也未修改选项字节。
