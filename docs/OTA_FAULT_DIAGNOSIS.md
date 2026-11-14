# OTA 健康确认与安装失败定位

## 本次实板日志能够说明什么

`autoState=health, autoChecks=0` 表示尚未开始查询后台。`error=5` 原来表示一次健康确认需要重试，旧代码成功后没有清除此值，因此不能仅凭重复出现的5判断每次都失败。

本项目的 `Alarm storage boot result 2` 表示 `ALARM_STORE_IO_ERROR`，来源可能是 EEPROM 访问或事务锁失败；空白日志和损坏日志分别是1、3。Boot 的 `state log unavailable or request retained` 同时包含多种情况，不能单凭这句判断 EEPROM 硬件损坏。

用户确认曾在安装时手动 RESET。之后20:50:09进入安装、20:51:24出现 `Install incomplete`，是另一次真实安装失败。旧日志没有失败阶段，尚不能确定是下载、Flash还是日志提交。Boot拒绝启动未经验证的半份应用，随后重新尝试。

电脑侧使用设备配置的局域网地址读取当前v2：67个正文分段全部返回正确的Range/长度/ETag，拼接出的272796字节与本地 `build/ota-packages/v2/clock-v2.ota` 完全一致。这证明该次电脑侧响应正确，不能替代板上ESP接收、Flash或EEPROM验证。

## 本次源码修复

- 健康确认成功后清除旧的错误5，保留人工请求错误1～4。
- 日志返回EMPTY/CORRUPT/INVALID时明确报告blocked，不再永久停留在health；不会跳过日志许可开始升级。
- 在临界区内获取心跳判断所用时刻，修复高优先级任务抢占后出现“时间倒置”导致的错误失活判断。
- OTA状态增加 `health`、`journal`；EEPROM保留最近一次真实HAL失败的操作、地址、长度、返回值、错误码和状态。
- Boot输出目标版本/长度和安装失败阶段、镜像偏移、Flash地址、日志结果，认证与安装顺序保持原有约束。

这些是已由主机回归复现或验证的源码修复及诊断增强，不代表已确认实板失败的具体原因。

## 诊断固件

已使用现有实际配置与同一发布密钥单独生成：

```text
build/ota-health-diagnostics/v1/clock-factory-v1.hex
```

原来 `build/ota-packages/v1/`、`v2/` 保留。这里继续使用版本1，以便安装后仍可尝试后台已发布的v2。诊断目录中的新版v1 `.ota` 不应上传到版本库；同版本不同内容会被后台拒绝。

需要重新生成时：

```powershell
.\scripts\build_ota.ps1 -Version 1 -Configuration Release -Factory -OutputDirectory build/ota-health-diagnostics/v1
```

输出目录已存在时使用新的 `-OutputDirectory`，脚本不会覆盖既有目录。诊断版仍保留原PWM蜂鸣器配置。

由于旧Boot缺少阶段日志，获取安装失败细节需要操作者用现有ST-LINK安装这份工厂HEX一次。以下命令仅供手动执行，本次代码工作没有连接或烧录开发板：

```powershell
& "$env:LOCALAPPDATA\stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe" `
  -c port=SWD freq=1000 `
  -w "D:\code\Clock_CubeMX\build\ota-health-diagnostics\v1\clock-factory-v1.hex" `
  -v
```

烧录校验完成后正常重新上电，松开KEY1，保持电脑后台的v2发布和服务运行。EEPROM中若仍保存v2安装目标，Boot会继续恢复该目标。下载时Boot没有LCD界面，过程中保持供电并等待日志，不通过反复RESET判断进展。

## 提供哪些新日志

安装失败时，记录这一行以及前后的Boot输出：

```text
[BOOT] Install failed: stage=fetch image_offset=8192 address=0x0 store=0
```

上面仅为格式示例，不是本次实板测量值。`image_offset` 不含256字节包头，实际HTTP偏移为该值加256。摘要失败时偏移表示已检查长度，不能据此定位单个坏字节。

| stage | 应继续检查的范围 |
| --- | --- |
| `permit-journal` / `trial-journal` | EEPROM许可保存或试启动提交，结合store结果 |
| `erase-footer` / `erase-application` | 内部Flash擦除 |
| `fetch` | 对应分段的ESP接收、HTTP响应与网络 |
| `program` / `program-readback` | 内部Flash编程或回读比较 |
| `image-read` / `image-vector` / `image-hash` | 完整镜像回读、向量或SHA256 |
| `footer-*` | 认证描述写入、末尾提交或认证回读 |

Boot日志的store编号为：0=OK、1=EMPTY、2=CORRUPT、3=IO、4=INVALID，与闹钟模块编号不同。

如果进入App但继续等待健康确认，记录完整OTA状态及后面的EEPROM行，例如：

```text
OTA {...,"health":"journal-read","journal":"io"}
[EEPROM] Last failure: op=read address=0x0800 length=128 HAL=2 error=0x00000020 state=0x00000020
```

同样仅为格式示例。`Last failure` 保留最近故障，成功传输不会清除，不能独立当作当前仍失败的证明。HAL返回值1/2/3分别为ERROR/BUSY/TIMEOUT；F4 HAL错误位 `0x04` 为AF无应答、`0x20` 为超时。`health=stale`、`lock`、`unavailable` 分别指心跳失活、事务锁不可用、本地服务快照不可用；`journal-read`、`journal-save` 分别区分日志读取和保存。`retry-pending` 表示旧失败自动请求具备重试条件，不表示日志已经CONFIRMED。

EEPROM中断事务、线路电平与外部上拉尚需实板证据。Boot和App的PB6/PB7内部上拉配置存在差异，但目前没有足够证据改变硬件假设或清空存储。

## 验证命令

```powershell
.\scripts\test.ps1 -IncludeLegacy
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
.\scripts\build.ps1 -Configuration Debug -Ota -OtaConfig app/ota_config_local.h
```

执行日志位于 `build/ota-health-*.log`。主机测试不能代替烧录诊断版后的实板失败阶段确认。

2026-11-14最终验证：上述完整测试入口通过，包含新增5个EEPROM诊断场景、11个服务场景、3个确定性抢占场景、安装器故障阶段和原有50项Node／遗留回归。普通及OTA的Debug／Release四种构建通过，Release App273444字节、Boot18660字节；签名、向量、容量和密钥隔离检查通过。独立源码审阅无重要问题。诊断版使用现有实际配置构建，未烧录、未上传后台或修改当前发布。

## 最后一块下载失败的后续修复

新实板日志显示 `stage=fetch image_offset=270336`、镜像长度272540，即66个4096字节块之后，最后2204字节失败。对应包Range为 `270592-272795/272796`，计算正确；该阶段失败发生在尾块写入Flash之前。

主机模拟同样大小的完整包复现了一个关闭通知依赖问题：旧下载器即使已收到全部2204字节、HTTP解析完整且主动关闭成功，仍会因为没有收到异步 `CLOSED` 而返回失败。修复后的规则为：

1. HTTP头、长度、Range和固定ETag均通过，正文精确收齐。
2. 只查询一次当前接收缓存，必须明确为零；无法查询或已缓存额外字节均拒绝。
3. 主动关闭TCP，成功确认后才交付正文；若关闭命令报错且未收到关闭通知，只能通过完整 `AT+CIPSTATE?` 回复确认没有活动连接。
4. 状态查询含非法字节、超长行、未知行、活动连接、错误或超时均拒绝，不从后续孤立的OK推断成功。

此流程符合[ESP-AT 4.1 ESP32-C3命令说明](https://docs.espressif.com/projects/esp-at/en/release-v4.1.0.0/esp32c3/AT_Command_Set/TCP-IP_AT_Commands.html#at-cipstate-obtain-the-tcp-udp-ssl-connection-information)中主动关闭与无连接查询的接口。固定目标、HMAC、完整镜像SHA及先关闭连接再写Flash的顺序不变。模拟已复现源代码缺陷，但板上是否由相同通知时序触发仍需重新安装后确认。

本次工厂镜像使用新的目录，包含上述Boot修复：

```text
build/ota-tail-fix/v1/clock-factory-v1.hex
```

继续使用现有配置与发布密钥、版本1，后台保持原v2发布。因为修复在Boot中，需要操作者通过ST-LINK安装这份合并HEX一次；只上传App不能更新Boot。可沿用前文烧录命令，将HEX路径改为该文件。原来的v1/v2包及上一份诊断镜像均保留；本次未连接或烧录开发板，也未改变后台发布。

Boot若再次失败，会额外打印 `Fetch response: received=... expected=... complete=... failed=... peer_closed=...`，用于区分未收齐、HTTP校验失败与关闭确认失败；每次请求开始前重置HTTP状态，避免误报上一块的接收计数。

网络回归包括真实大小完整下载、尾响应总长2559/2560/2561与关闭通知先后顺序、丢失通知、关闭与查询失败，以及截断/错误身份/错误范围/同帧和跨帧额外数据。证据日志 `build/ota-tail-red.log`、`build/ota-tail-protocol-red.log` 和 `build/ota-tail-final-net.log`。

最终 `scripts/test.ps1 -IncludeLegacy` 全部通过（含50项Node），OTA Debug／Release构建与密钥隔离检查通过。Release App273444字节、Boot19100字节。完整日志为 `build/ota-tail-final-tests.log`、`build/ota-tail-ota-debug.log`、`build/ota-tail-ota-release.log`、`build/ota-tail-artifacts.log`。独立审阅确认跨帧额外数据与非法状态行均被拒绝，无重要问题遗留；实板成功仍待操作者验证。

## 正文收齐后仍失败：记录实际 AT 应答

21:37:50实板使用上一版后仍失败：`received=2204 expected=2204 complete=1 failed=0 peer_closed=0`。这证明尾块正文和HTTP范围校验通过，但无法区分最后一次 `AT+CIPRECVLEN?` 查询失败与关闭确认失败；尚未进行完整镜像SHA验证。上一版关闭处理修复没有解决这次实板故障，不能将其描述为已经修复成功。

本次只增加诊断，不改变安装放行条件。新日志在原有两行后增加：

```text
[BOOT] Fetch finish: pending_checked=1 pending_ok=0 remaining=0 close_ok=1
[BOOT] AT pending RX: ...
[BOOT] AT close RX: ...
[BOOT] AT state RX: ...
```

以上数值仅为格式示例。`pending_ok=0` 时 `remaining` 无效；`pending_ok=1 remaining=0 close_ok=0` 才能明确归入关闭确认失败。三个RX字段保留各步骤实际消费的控制应答，回车、换行、反斜杠和非打印字节均转义，最多各96字节；截断时标记 `[truncated]`，未读取字节时显示 `[empty]`。原解析器若提前退出，后续字节可能出现在下一步骤，这是诊断需要保留的证据。

接收时只将字节写入RAM，安装失败后才向调试串口打印，不同步转发正文，不打印请求、Wi-Fi密码或密钥。只有正文与最后一个被动接收帧完整结束、进入末次缓存查询后，才开启这些原始应答记录；接收中途超时后的迟到正文不会被关闭诊断收录。每次有效Range开始前清除记录。诊断不得把异常应答、空字段或未知状态默认为零，也不得用正文收齐代替关闭确认或镜像认证。

本次验证镜像路径：`build/ota-at-diagnostics/v1/clock-factory-v1.hex`。操作者通过ST-LINK烧录该合并HEX后，继续使用后台现有v2；等待一次完整失败或成功，不在下载期间RESET。若失败，保留从 `Install failed` 到三条 `AT ... RX` 的所有日志。

最终验证：新增诊断场景及完整 `scripts/test.ps1 -IncludeLegacy` 通过，包含50项Node测试；OTA Debug／Release与产物密钥隔离检查通过，App273444字节、Boot19716字节。证据为 `build/ota-at-diagnostic-red.log`、`build/ota-at-payload-red.log`、`build/ota-at-diagnostic-final-net.log`、`build/ota-at-diagnostic-final-tests.log`、`build/ota-at-diagnostic-debug.log`、`build/ota-at-diagnostic-release.log`、`build/ota-at-diagnostic-artifacts.log`。独立审阅发现并确认修正了帧中途超时后记录迟到正文的问题，复审无重要问题。本轮未连接或烧录开发板，实板失败原因仍等待新控制应答。
