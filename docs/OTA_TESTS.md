# 单应用分区 OTA 使用与验收

本功能针对 STM32F407VET6、现有 ESP32-C3 MINI-1/ESP-AT 4.1.1 和 BL24C512，不增加硬件。不自动烧录，不修改选项字节。首次上板仍需通过已有 SWD 工具安装工厂 HEX。

局域网管理后台、统一构建脚本和开机自动检查的最新流程见 [局域网 OTA 后台操作说明](OTA_ADMIN_TESTS.md)。下文保留底层分区、人工工具和恢复机制说明。

## 模式与限制

普通 `Debug`/`Release` 仍为原地址独立应用。只有 `-Ota` 或 OtaDebug/OtaRelease 构建包含重定位应用和 Bootloader。IAR OTA 构建尚不支持，启用时明确报错；IAR 独立链接范围修正为 VE 的 512 KiB，不能用旧入口的 BIN 打 OTA 包。

OTA 升级期间主应用暂停。下载/安装中断后从头下载，不是断点续传，也没有本地旧版回滚。旧 App 被覆盖后，没有网络/服务器时留在 Bootloader 等待恢复。未增加看门狗，应用死锁时仍需 RESET 或整板重新上电；三次未确认复位后自动恢复不重装同一个失败包，需发布另一个认证包或按 KEY1 强制重试。

固件通过 HTTP 传输，以 HMAC-SHA256 校验发布者、SHA-256 检查完整内容。发布密钥仅编译进不通过 OTA 发布的 Bootloader；可下载 App 只读取已由 Boot 验证的元数据，不包含发布密钥。App 保存的是管理令牌的 SHA-256 验证值，不保存原令牌。发布密钥和网页管理令牌独立，属于软件凭据；不承诺抵抗有物理读写权限的攻击。版本允许受控降级以远程修复。

| 区域 | 起始 | 末地址，含 |
| --- | --- | --- |
| Bootloader | `0x08000000` | `0x0800FFFF` |
| App 与资源 | `0x08010000` | `0x0807FEFF` |
| 认证安装描述 | `0x0807FF00` | `0x0807FFFF` |
| EEPROM 闹钟日志 | `0x0000` | `0x07FF` |
| EEPROM OTA 双槽日志 | `0x0800` | `0x0FFF` |

App BIN 最大 458496 字节。Bootloader 固定优化并由链接器限制在 64 KiB。所有启动都验证认证描述、完整实际镜像和精确向量范围。

## 一次性配置

确保现有 `app/clock_config_local.h` 中的 Wi-Fi 凭据正确。Bootloader 编译时直接包含这份本地配置，不从 App 提取凭据。OTA 配置和密钥 JSON 均被 Git 忽略。

在仓库根目录执行，将主机地址改为电脑在设备所在局域网的固定地址：

```powershell
node tools/ota/ota.mjs init --config app/ota_config_local.h --key-file app/ota_key_local.json --host 192.168.1.10 --port 8788 --path /firmware.ota --version 1
.\scripts\build.ps1 -Configuration Release -Ota
```

初始化拒绝覆盖已有文件。私下备份两份配置，后续版本必须保持与已安装 Bootloader 相同的发布密钥。更换发布密钥需要重新安装对应 Bootloader，不能只给旧 Bootloader 发布新密钥签出的包。

管理令牌在本地 JSON 的 `adminTokenHex` 字段；仅由操作者私下读取并输入网页，不在日志、状态接口或网页源码显示。发布密钥在 `keyHex` 字段，不要发到聊天、提交 Git 或放在 HTTP 文件目录。

可用 `-OtaConfig <自定义头文件>` 指定另一个配置，工具也支持任意显式输出路径；自定义秘密路径应自行忽略。缺少发布密钥或恢复主机配置时 OTA 构建报错，不生成未认证替代固件；缺少管理令牌时只禁用网页触发。

## 打包与首次安装

```powershell
node tools/ota/ota.mjs pack --app build/OtaRelease/Clock_CubeMX.bin --boot build/OtaRelease/Clock_Bootloader.bin --key-file app/ota_key_local.json --version 1 --output build/OtaRelease/clock-v1.ota --factory-output build/OtaRelease/clock-factory-v1.hex --report build/OtaRelease/clock-v1-report.json
```

版本必须是正 u32。输出使用创建模式，已有文件不会被覆盖；新版本更换输出名称。普通独立应用 BIN 因复位向量地址不对会被拒绝。工厂 HEX 包含 Bootloader、App、认证安装描述三段，不包含 EEPROM 数据。

首次使用现有烧录器安装 `clock-factory-v1.hex`，保持 BOOT0 的正常 Flash 启动状态，不刷 ESP、不改接线、不擦外部 EEPROM。不要只刷 `Clock_CubeMX.bin`，它是从 `0x08010000` 运行的重定位应用。烧录器若提供整片擦除，会清除内部旧应用；已有闹钟保存在独立 EEPROM，仍应按正常备份流程维护。

上电后串口应出现 `[BOOT]` 及完整镜像验证提示，然后出现时钟应用日志。首次 TRIAL 启动会记录次数；本地 UI、闹钟、网络任务创建并稳定运行十秒后保存 CONFIRMED，不要求 Wi-Fi 或 OneNET 在线。

## 发布后续版本

构建新应用，使用相同密钥及新版本整数生成新 `.ota`。不需要再次提供 `--boot/--factory-output`：

```powershell
node tools/ota/ota.mjs pack --app build/OtaRelease/Clock_CubeMX.bin --key-file app/ota_key_local.json --version 2 --output build/OtaRelease/clock-v2.ota
node tools/ota/ota.mjs serve --input build/OtaRelease/clock-v2.ota --key-file app/ota_key_local.json --host 0.0.0.0 --port 8788 --path /firmware.ota
```

服务器读取并冻结一份完整认证包，只暴露指定路径，不暴露文件目录或密钥。允许该局域网端口入站，确认 AP 未开启客户端隔离。`0.0.0.0` 只用于服务器监听，设备配置使用电脑真实地址。切换包时停止服务器后重新启动；替换磁盘文件不会改变已经冻结的当前发布。

在现有时钟网页的“固件升级”区域输入管理令牌并确认。POST 的 202 仅表示 queued；网页轮询到 saved 才表示升级请求已持久化，设备约两秒后重启。网页断线期间通过服务器日志与串口查看下载；设备回来后刷新，以认证安装描述的实际版本和状态为准，不能把网络断线当作升级成功。

本地编辑、保存、响铃或贪睡冲突时拒绝准入。EEPROM 写入/回读不明确时不复位。升级状态码中的错误编号为：1 维护准入失败、2 事务锁不可用、3 请求日志失败、4 队列失败、5 健康确认待重试；Boot 日志的网络预检/安装失败分别记录为1/2，应结合日志阶段解读。

## 恢复模式

在 RESET/上电时稳定按住 KEY1（PA0、低有效）约两秒，强制进入固定恢复入口下载。保持原有 H1 跳帽约定，不改接线。Bootloader 只使用串口提示，不初始化 LCD。

断电后原安装描述缺失或实际摘要错误时，不启动半份 App。恢复下载每次重新认证包，最多4096字节一块，读完并关闭TCP后才擦写，期间不用 App 的 FreeRTOS 驱动。服务端 Range/总长/ETag 不匹配即拒绝，不拼接不同包。

尚未擦写且旧 App 完整时，网络预检失败可以返回旧 App，升级请求保留。已被覆盖时只能继续等待恢复网络/存储。EEPROM 严重损坏或失联时不会猜测空白并开始擦写；已有认证完整 App 可报告 OTA 不可用并继续正常启动，否则保留 Bootloader，需修复故障或通过现有 SWD 维护。

ESP 可能在 MCU 单独复位时仍等待数据，Boot 会有限填充、补 CRLF、排空并软重启同步。现有板没有受控 ESP EN/RST；ESP 完全失去 AT 响应时，必须给整板重新上电。

## 自动验证

需要现有 ARM GCC/CMake/Ninja、MSVC C++ Build Tools、Node.js 20+：

```powershell
.\scripts\test.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
.\scripts\build.ps1 -Configuration Debug -Ota
.\scripts\build.ps1 -Configuration Release -Ota
```

`scripts/test.ps1` 只依赖纳入版本管理的 BSP/OTA 与工具测试；新克隆可复现。当前工作区另有被忽略的历史完整测试，可通过 `-IncludeLegacy` 追加，未将其冒充为仓库自带测试。

验证覆盖标准密码摘要向量、跨语言认证、包字段/内容篡改、EEPROM 页写/提交失败与应答丢失、安装边界断电、最大镜像、错误向量、AT 慢速/二进制分包/输入窗、HTTP Range/身份、应用请求/维护并发/健康确认、包服务器和工厂 HEX。

本次构建验证使用 `build/ota-validation/` 内独立生成的配置和密钥，恢复地址为示例 `192.168.1.10`。这些产物用于编译和协议检查；上板前应按“一次性配置”生成自己的配置并重新构建打包。没有替换已有 Wi-Fi/OneNET 凭据。

可对实际二进制检查链接向量、容量和下载固件中的秘密隔离：

```powershell
node scripts/verify_ota_artifacts.mjs build/OtaRelease/Clock_CubeMX.bin build/OtaRelease/Clock_Bootloader.bin app/ota_key_local.json
```

## 审阅后的明确边界

网络层各 AT 操作均有界，但15秒读取循环与60秒预检返回预算是外层检查，已经开始的内层命令可使整体耗时略超该预算；不是精确的实时截止保证。底层 Flash 适配器拒绝 Boot 区及芯片外地址；App 正文与安装描述间的跨区限制由公共安装器强制执行，底层适配器只供该安装器调用。两项为已记录的防御性改进余项，不影响已验证的安装提交顺序。

## 必须实板验收的项目

| 项目 | 操作与应有结果 |
| --- | --- |
| 首次启动与交接 | 工厂HEX上电；Boot完整认证后进入App，十秒后确认，向量和中断正常 |
| 离线运行 | 正常确认后断Wi-Fi重启；时钟和闹钟仍启动，不等待云端 |
| 正常OTA | 发布v2；网页queued/saved后重启下载；回来后版本为v2，原闹钟保持 |
| 请求保存失败 | 模拟EEPROM读/写失败或ACK丢失；网页不报成功，不触发擦除/复位 |
| 断电重下 | 日志提交、扇区7擦除、其余擦除、正文写入、footer提交阶段分别断电；半成品不启动 |
| 断网与服务器变化 | 下载时停服务器/换包；不混合镜像，恢复后重新完整认证安装 |
| 试启动限制 | 未确认应用连续复位三次；停在恢复模式，等待不同包或KEY1明确强制 |
| KEY1恢复 | 在RESET时按KEY1两秒；可通过固定服务器修复应用 |
| AT恢复 | MCU单独复位、ESP输入窗中断；可重新同步；ESP彻底无响应时整板重上电 |
| 长时间回归 | 时钟/温湿度/天气/按键/网页/OneNET/闹钟并发，观察堆与栈低水位，无假保存确认 |

主机测试、ELF检查与编译不能代替上述硬件测试。本任务未连接或烧录开发板，不把这些项目报告为已通过。
