# OneNET 接入准备：设备认证与 ESP 固件探测

已按控制台记录配置公开参数：

| 项目 | 值 |
|---|---|
| 产品 ID | `4lh76atTpR` |
| 设备名称 / MQTT Client ID | `clock001` |
| MQTT Username | `4lh76atTpR` |
| 普通 MQTT | `4lh76atTpR.mqtts.acc.cmcconenet.cn:1883` |
| TLS MQTT，后续首选 | `4lh76atTpR.mqttstls.acc.cmcconenet.cn:8883` |
| 数据协议 | OneJSON |

端口依据 [OneNET 智能域名说明](https://iot.10086.cn/doc/aiot/fuse/detail/1788)。**本文保留 PREP 阶段记录及本地 Token 配置方法。当前代码已更新为 `v1.4-ONENET`，包含实际 TLS MQTT 连接、属性上报及持久化闹钟设置；请按 [MQTT 上板与控制台验收](ONENET_MQTT_TESTS.md) 使用新版。** 以下“connection not started”等日志仅描述旧准备版，不能用来判断新版状态。真实上线仍须烧录后验证。

## 1. 在本地生成 Token

从 OneNET **设备详情**复制 `clock001` 的设备密钥；此流程使用一机一密，不使用产品 AccessKey。不要把密钥发到聊天、源码或日志中。

在工程根目录的 PowerShell 中运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure_onenet.ps1
```

工具会提示粘贴设备密钥，输入被隐藏。它在本机生成设备级 HMAC-SHA256 Token，写入：

```text
app/onenet_config_local.h
```

该文件已加入 `.gitignore`，只保存生成的 Token 与有效期，不保存原始设备密钥；终端不打印密钥或 Token。既有 `app/clock_config_local.h` 中的 WiFi、天气配置保持原样。

默认 Token 有效期为 30 天。需要改变测试期限可使用 `-ValidDays 7` 等参数；过期后重新生成、重新编译和烧录。Token 算法参考 [OneNET 官方说明](https://iot.10086.cn/doc/aiot/fuse/detail/1486)，使用资源 `products/4lh76atTpR/devices/clock001`，签名字符串按 `et\nmethod\nres\nversion` 组织，参数值经 URL 编码。

脚本也提供 `-KeyFile` 供本地自动化读取密钥文件；该文件必须自行妥善保管。日常操作建议使用默认隐藏输入。`-OutputFile` 供测试使用，若改写到其他位置，该位置不一定受工程 `.gitignore` 保护。

## 2. 编译并上板取得固件信息

```powershell
.\scripts\build.ps1 -Configuration Release
```

烧录 `build/Release/Clock_CubeMX.hex`，USART1 使用 **115200、8N1**。不需要修改焊接板接线，也不会重新烧录 ESP 固件。

启动后寻找：

```text
[SYS] Clock_CubeMX v1.3-WEB+ONENET-PREP ...
[ESP] Firmware information:
AT version:...
SDK version:...
...
[ONENET] MQTT state query accepted; ...
[ONENET] Prepared target ...:8883; ...; token configured; connection not started
```

还没生成本地 Token 时会显示 `token missing`，本地时钟、按键、闹钟、天气、温湿度和已有局域网页仍可运行。

探测命令是只读的 `AT+GMR` 和 `AT+MQTTCONN?`：

- `MQTT state query accepted`：只说明查询被接受，不能据此断言长密码、证书或全部 TLS 命令均已支持。
- `MQTT state query rejected`：可能不支持，也可能固件要求先初始化；仍继续原有 WiFi、SNTP 和天气功能，需要结合版本核对。
- `MQTT probe transport failure`：属于超时/串口失步，原网络恢复机制会重新初始化；本地任务继续。

下一步只需要提供这些版本与探测日志；日志不会包含 OneNET Token 或设备密钥。ESP 长 Token/TLS 命令因固件版本而异，参考 [ESP32C3 MQTT AT 文档](https://docs.espressif.com/projects/esp-at/en/latest/esp32c3/AT_Command_Set/MQTT_AT_Commands.html)。

## 3. 后续 MQTT 调试闭环

确认版本与证书方式后，才接入连接、订阅、OneJSON 温湿度上报、8 组闹钟下发和 EEPROM 保存后确认。MQTT/网页/按键会复用现有闹钟服务；不在网络回调中直接写 EEPROM。

OneNET 属性设置的回复有时间限制，现有天气长事务也需一并调整，不能把 AT `OK` 或请求已排队当成配置保存成功。当前准备版不处理这一业务闭环。

## 验证

```powershell
.\tests\onenet_token_test.ps1
.\tests\run_host_tests.ps1
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
```

Token 测试使用公开虚构密钥及由独立 Python 标准库计算的固定签名，不使用真实凭据。ESP 主机测试覆盖完整/超长固件回复、输出容量、查询拒绝、超时与 UART/TX 故障，以及拒绝后保留 SNTP、天气和局域网页能力。主机替身和编译不能证明真实 ESP 指令能力、TLS 证书或 OneNET 在线状态。

2026-11-13 准备版验证：全套主机测试 exit0（含新增 Token、8 个应用场景、ESP 双上电状态与原全部回归）；Debug/Release 编译链接无警告。主 RAM115872B、CCM5984B；Flash Debug281432B / Release243184B。Windows PowerShell5.1 的脚本语法和签名固定向量已独立核对。日志在 `build/onenet-prep-validation/`。未使用真实设备密钥、未连接 OneNET、未烧录开发板、未提交 Git。
