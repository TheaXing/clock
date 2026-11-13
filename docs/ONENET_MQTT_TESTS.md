# OneNET MQTT 控制台与实板验收

本指南适用于当前完整固件 `v1.4.3-ONENET`：ESP32-C3 负责 MQTT/TLS，STM32 复用现有闹钟服务与唯一 EEPROM 保存任务，实现室内温湿度上报和本机 8 组闹钟控制。先完成“控制台定义 → 手动烧录 → 一组闹钟闭环”，再做后面的故障与并发测试。

**真实开发板的 TLS 握手、OneNET 在线、云端保存往返尚待按本指南验收。** 主机替身、编译成功或公开文档只能验证相应边界，不能代替这些实板结果。保持 H1 现有焊接状态；沿用原按键、蜂鸣器、传感器与屏幕连接，不需要重刷 ESP 固件。

### v1.4 启动时配置失败的修正

用户上板报告 `TLS/credential configuration failed, transport error 3`。该数字仅是工程中的“指令被拒绝”，旧日志未记录具体步骤。核对 [Espressif 官方 v4.1.1.0 ESP32-C3 库](https://github.com/espressif/esp-at/blob/v4.1.1.0/components/at/lib/libesp32c3_at_core.a) 确认：MQTT 尚未初始化或已经清理时，`AT+MQTTCLEAN=0` 会返回 `ERROR`；开启 SYSLOG 后其原始码为 `0x010A6003`。旧代码在 USERCFG 之前要求 CLEAN 必须 OK，会阻止这种首次配置。

`v1.4.1-ONENET` 先开启 `AT+SYSLOG=1`，只对 CLEAN 的明确 `ERROR + 0x010A6003` 且串口仍同步的情况继续；没有原始码的 ERROR、其他错误、BUSY、超时或重启均不放行。CA、USERCFG、长密码、SNI 和连接参数仍须全部成功；TLS 验证方式保持 scheme 3。官方错误提示方式见 [SYSLOG 文档](https://docs.espressif.com/projects/esp-at/en/release-v4.1.0.0/esp32c3/AT_Command_Set/Basic_AT_Commands.html#at-syslog-enable-or-disable-the-at-error-code-prompt)。

新版继续保留该修正和错误诊断。若失败，日志会显示固定步骤、应答种类和数值错误码，例如下面是假设 USERCFG 失败的**格式示例**：

```text
[ONENET] TLS/credential configuration failed, transport error 3; step=MQTTUSERCFG; reply=ERROR; esp=0x010A6004; retry pending
```

需要反馈的是这一整行及前面的 WiFi/SNTP 日志；无需向调试串口手动发送 AT，也无需先更换 Token。该修正对应的主机复现已先观察到失败，再修复通过；仍须通过本次板上日志确认是否解决用户的实际失败步骤。

### v1.4.2：连接阶段的证书名称兼容

用户随后报告配置已通过，`MQTTCONN` 返回 `ERROR`、`esp=0x00000000`。官方 v4.1.1 库的运行期连接错误路径只输出通用 ERROR，没有将 TLS/套接字/CONNACK 的底层原因传到 AT 指令串口；这里的零表示没有取得原始错误码，不表示连接没有错误。

2026-11-13 的无凭据公共网络检查确认：产品域名的 TCP8883 可达，使用工程内官方 CA、保留证书验证、无 SNI 时 TLS1.2 握手成功；启用产品域名匹配则因证书名称不符而失败。服务端证书是 `CN=OneNET MQTTS` 的自签名证书，没有产品域名 SAN，且与官方下载的证书相同。该测试没有发送 MQTT CONNECT，也不能代表开发板所在网络或证明认证已成功。

核对 [官方 ESP-AT v4.1.1 ESP32-C3 库](https://github.com/espressif/esp-at/blob/v4.1.1.0/components/at/lib/libesp32c3_at_core.a) 和 [对应 esp-mqtt 配置结构](https://github.com/espressif/esp-mqtt/blob/cac1552e62b0474c162547b7cce345d7cd1aecfe/include/mqtt_client.h) 确认：此版本非空 MQTTSNI 同时用作证书名称，未设置 SNI 时跳过证书名称匹配。旧配置强制使用产品域名，会与当前官方证书不匹配。

当前 OneNET 接入明确使用**专用官方 CA + 证书链/有效期校验，不发送 SNI，不验证产品 DNS 名称**。scheme3 保持不变，没有自动降为 scheme2 或明文。通用驱动新增可选 `tls_server_name`：NULL 保留原主机名校验，非空设置显式名称，空串明确选择上述专用 CA 策略；本工程仅 OneNET 选择空串。每次配置仍先 CLEAN，清除上次连接保留的 SNI。

此策略继续保留在新版，且能看到 `TLS policy: OneNET CA/date verification; no SNI or DNS-name check`。用户 v1.4.2 板上日志随后显示 MQTTCONN 已返回 OK，失败点推进到了状态解析，见下一节。

产品保持“开发中”即可进行这一阶段的接入与设备调试，官方流程先进行设备调试再发布量产，见 [产品开发流程](https://iot.10086.cn/doc/aiot/fuse/detail/895)。确认测试设备位于 `clock` 产品 `4lh76atTpR` 下。用户已确认采用 `clock001` 设备密钥，平台当前“未激活”与尚未完成首次连接相符。

### v1.4.3：状态查询的带引号端口

用户板上 v1.4.2 日志确认 SNTP 时间、南京天气、AHT20 和 EEPROM 启动正常；MQTTCONN 已返回 OK，随后为 `transport error 6; step=MQTTCONN_STATE; reply=OK`。这表示模块接受了连接/查询，工程没有通过后续状态校验，不能单凭这行判断平台已接受属性。

核对官方 v4.1.1 库的 `at_query_cmd_mqtt_connect` 格式串为 `%s:%d,%d,%d,"%.*s","%.*s","%.*s",%d%s`。真实状态包含带引号的端口，例如：

```text
+MQTTCONN:0,4,3,"4lh76atTpR.mqttstls.acc.cmcconenet.cn","8883","",0
OK
```

旧代码只接受端口 `8883`，会拒绝 `"8883"`。v1.4.3 同时接受这两种数字格式，仍要求非空、最多 5 位数字、引号成对、逗号分隔、端口匹配；状态须为 4…6、scheme3、主机完整匹配且期间未收到断连。没有把单独的 OK 当成在线。

重新烧录后先确认 `v1.4.3-ONENET`，再核对连接和订阅完成、平台在线及 `Cloud accepted report`。版本修复仍需这次真实板上验收；无需先更换已有设备密钥或发布产品。

## 1. 先定义控制台属性

在现有 OneNET 产品的功能定义/物模型中核对以下属性。控制台名称可能随版本调整，以“产品功能定义”和“设备属性调试”这两个功能为准。使用 **MQTT / OneJSON**；不要改为数据流透传或另建服务调用。

公开连接参数来自 `app/onenet_config.h`：

| 项目 | 当前默认值 |
|---|---|
| 产品 ID / MQTT Username | `4lh76atTpR` |
| 设备名称 / MQTT Client ID | `clock001` |
| TLS 地址 | `4lh76atTpR.mqttstls.acc.cmcconenet.cn` |
| 端口 | `8883` |
| Password | 本机生成的设备级 Token，不能填写原始设备密钥 |

### 顶层属性

标识符必须逐字一致，区分大小写。显示名称可用中文。

| 标识符 | OneNET 数据类型 | 权限 | 本固件含义与范围 |
|---|---|---|---|
| `temperature` | Float | 只读 | 室内温度，单位 ℃；仅上报有效的有限数值，协议未额外设置温度上下限 |
| `humidity` | Float | 只读 | 室内相对湿度，单位 %RH，`0..100` |
| `sensor_valid` | Bool | 只读 | 当前室内测量是否有效，JSON 为 `true` / `false` |
| `config_revision` | Int64 / Long | 只读 | 已确认 EEPROM 日志代数；实际值为无符号 32 位范围 `0..4294967295`，云端用 64 位整数容纳 |
| `ringing_mask` | Int32 | 只读 | 当前响铃位图，`0..255`；bit0 对应第 1 组，bit7 对应第 8 组，`0` 表示当前没有这 8 组的响铃 |
| `alarm1`…`alarm8` | Struct | 读写 | 分别对应本机第 1…8 组；每组均配置下面相同的 9 个成员 |

`config_revision` 是设备存储代数，不是 OneJSON 的 `version`，也不是云端可填写的乐观锁参数。没有额外的 `expected_revision` 可写属性。

### 每组 Struct 的 9 个成员

在 `alarm1` 到 `alarm8` 下分别定义，成员标识符保持一致；不是把所有闹钟放进一个数组。

| 成员标识符 | 类型 | 范围 / 步长 | 含义 |
|---|---|---|---|
| `enabled` | Bool | `true` / `false` | 启用开关；下发不得用字符串或整数代替布尔值 |
| `hour` | Int32 | `0..23`，步长 1 | 小时，设备时间为 UTC+8 |
| `minute` | Int32 | `0..59`，步长 1 | 分钟 |
| `repeat_mask` | Int32 | `0..127`，步长 1 | bit0 周一、bit1 周二，依次到 bit6 周日；`31` 工作日，`96` 周末，`127` 每天，`0` 指定日期一次 |
| `year` | Int32 | `2020..2099`，步长 1 | 指定日期的年 |
| `month` | Int32 | `1..12`，步长 1 | 月 |
| `day` | Int32 | `1..31`，步长 1 | 还必须是所选年月中的真实日期，例如 2028-02-29 合法、2026-02-29 非法 |
| `ring_seconds` | Int32 | `5..600`，步长 1 | 本组最长响铃秒数 |
| `snooze_seconds` | Int32 | `60..1800`，步长 60 | 贪睡 1…30 分钟，协议单位仍是秒 |

整数成员只接受 JSON 整数；`7.0`、`7e0`、`"7"` 均不接受。即使按星期重复，年月日也必须保持合法。非法字段、重复键、未知成员、只读属性写入，以及一次同时设置两组或更多闹钟，均整体拒绝，不会只保存其中一部分。

## 2. 本地 Token、完整编译与手动烧录

设备密钥的隐藏输入、本地 Token 生成与有效期更新，沿用 [接入准备文档的本地 Token 流程](ONENET_CONNECTION_PREP.md#1-在本地生成-token)。该文档记录上一阶段准备版；当前 MQTT 行为与验收以本指南为准。

在工程根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\configure_onenet.ps1
.\scripts\build.ps1 -Configuration Release -Clean
```

Token 只保存在被 Git 忽略的本地配置中；不要把设备密钥、Token、含凭据的配置文件或完整 MQTT 密码上传到聊天、日志或仓库。更新 Token 后必须重新编译并手动烧录，运行中的设备不会自动读取电脑上的新文件。

手动烧录 `build/Release/Clock_CubeMX.hex`。如使用 BIN，文件为同目录 `Clock_CubeMX.bin`，起始地址 `0x08000000`。默认是完整时钟、网页、MQTT 与 PWM 蜂鸣器版本，**不要加 `-RgbTest`**；实物此前确认使用有源蜂鸣器时，沿用 `-ActiveBuzzer` 构建参数。此步骤只烧录 STM32 应用，不涉及 ESP 固件或 H1 接线。

打开 USART1，参数 **115200、8N1**，从复位开始保存串口日志。核对启动版本为 `v1.4.3-ONENET`、编译时间为本次构建，再观察下面的连接过程。

## 3. 先确认连接，再确认平台收到属性

建议保留以下几类日志，参数以当次输出为准：

```text
[SYS] Clock_CubeMX v1.4.3-ONENET; ...
[ESP] Firmware information:
AT version:...
SDK version:...
[ONENET] Target ...:8883; product ...; device ...; token configured; connection follows WiFi/SNTP
[SNTP] ... UTC+8
[ONENET] Connecting TLS ...:8883; product ...; device ...
[ONENET] TLS policy: OneNET CA/date verification; no SNI or DNS-name check
[ONENET] MQTT connected and subscribed; property reports pending
[ONENET] Cloud accepted report ...
```

用户先前提供的模块信息为 **ESP32-C3 MINI-1、ESP-AT 4.1.1**。仍应保存本次 `AT+GMR` 对应的版本日志；`MQTT state query accepted; TLS MQTT setup follows WiFi/SNTP` 只说明 `AT+MQTTCONN?` 查询成功，不能单独证明证书写入、长 Token、TLS 或云端在线均已成功。

当前传输实现先读取 ESP 的 `mqtt_ca`，与工程内 OneNET 官方公开 CA 逐字节比较；不同才通过 `AT+SYSMFG` 写入，并回读核对。随后使用 **TLS scheme 3**、上述明确的专用 CA 验证策略、`AT+MQTTLONGPASSWORD` 分开发送 Token，再连接。OneNET 默认省略 MQTTSNI。工程的 Token 长度上限为 **1024 字节**；不会把长 Token 挤进受 AT 命令长度约束的普通密码参数。连接前要求 ESP 已取得有效 SNTP 时间。这些命令与证书验证要求见 [Espressif ESP32-C3 release-v4.1.0.0 MQTT 文档](https://docs.espressif.com/projects/esp-at/en/release-v4.1.0.0/esp32c3/AT_Command_Set/MQTT_AT_Commands.html)。

CA 来源及公开文件 SHA-256 记录在 `app/onenet/onenet_ca.c`，下载入口为 [OneNET MQTT 设备连接](https://iot.10086.cn/doc/aiot/fuse/detail/919)。2026-11-13 重新下载的 PEM 与工程证书逐字节一致。当前实现要求 CA 回读成功后才继续。**代码和公共网络探测不能代替开发板握手、MQTT 认证与实际属性上报验收。**

该专用 CA 策略不提供产品域名的独立证书名称验证。若平台更换证书，应重新核对官方 CA 与名称策略；不要通过自动切换 scheme2 处理失败。仍失败时保留失败阶段日志；本版裸 ERROR 的底层 TLS/网络/认证原因可能需要 ESP 日志串口或支持详细 MQTT 错误的固件进一步分辨。

验收分两步：

1. 看到 `MQTT connected and subscribed`，并在 OneNET 设备详情核对设备在线。这说明连接与本次订阅完成。
2. 看到 `Cloud accepted report <id>`，在控制台找到对应实际属性与更新时间。这说明平台对该次属性上报回复了 `200`；单独的 AT `OK` 或 `+MQTTPUB:OK` 不能证明物模型校验通过。

连接后会补报属性；正常运行约每 10 秒安排一次遥测更新。当前固件逐个属性发送，发送间隔至少 250 ms；实际刷新速度还受网络和 AT 往返影响。闹钟保存成功后会安排闹钟与存储代数回报，本地按键、网页保存也通过相同存储代数触发同步。

### 传感器无效时怎么看

启动尚无有效 AHT20 测量、测量失败或数值无效时，设备上报 `sensor_valid:false`，并停止把温度、湿度当作当前有效测量上报；不会用 `0`、`NaN` 或旧读数伪装新测量。控制台可能保留之前的温湿度显示，应结合 `sensor_valid` 和更新时间判断。

此时请求包含 `temperature` 或 `humidity` 的 GET 会整体回复 `503 / sensor_unavailable`。需要核对闹钟时，改为只读取 `alarm1`…`alarm8`、`config_revision` 或 `sensor_valid`。恢复正常测量后，下一轮遥测再更新温湿度。

## 4. 用控制台完成第一组保存闭环

先只操作一组，例如 `alarm1`；核对其他 7 组当前值作为基线。先保存关闭状态，再测试定时响铃，避免测试过程中意外触发。

如果控制台要求输入所选 Struct 的值，填写这一级对象：

```json
{
  "enabled": false,
  "hour": 7,
  "minute": 30,
  "repeat_mask": 31,
  "year": 2026,
  "month": 11,
  "day": 13,
  "ring_seconds": 60,
  "snooze_seconds": 300
}
```

如果编辑框要求整个属性参数对象，则外面加 `alarm1`：`{"alarm1":{...上述成员...}}`。示例日期是合法格式示例，做一次性定时测试时请改成设备当天或将来的实际日期。

只切换开关时允许部分成员更新。完整 OneJSON 下行示例是：

```json
{"id":"1001","version":"1.0","params":{"alarm1":{"enabled":false}}}
```

控制台通常自行生成 `id`、`version`、`params` 外层，不要把完整外层再嵌到 Struct 值中。若输入框只编辑属性参数，填写 `{"alarm1":{"enabled":false}}`；若只编辑 `alarm1` 本身，填写 `{"enabled":false}`。

部分成员会由设备补齐、验证，并在保存任务中只把**明确下发的成员**合入新读取的 EEPROM 配置；没有下发的成员保留存储值。因此显式 `enabled:false` 即使等于故障启动后的 RAM 默认值，也仍是关闭指令。首次调试建议发送完整 9 成员以建立清楚的基线，随后再测试部分更新。

消息 ID 必须是 **1…13 位十进制数字组成的字符串**，例如 `"1001"`；不能用 JSON 数字 `1001`。`version` 可省略，提供时必须是字符串 `"1.0"`。当前 MQTT 消息不带额外的 `method` 或存储版本字段。

成功时，设备发到 `set_reply`：

```json
{"id":"1001","code":200,"msg":"success"}
```

同时核对串口：

```text
Alarm configuration saved, generation ...
[ONENET] Alarm 1 request 1001 success
```

**`200` 只在 EEPROM 日志写入、回读验证和运行配置应用都成功后产生。** 已接收、已排队、AT 发布成功都不等于保存成功。随后通过属性 GET、局域网页或板端列表核对该组和其余 7 组，再按 RESET、断主电重启各核对一次。整个过程不要直接擦除外部 EEPROM。

## 5. 主题与三种 JSON 形状

下表把 `P` 记为 `$sys/4lh76atTpR/clock001/thing/property`。改过公开产品/设备配置时，主题随之改变。协议依据 [OneNET 属性/事件](https://iot.10086.cn/doc/aiot/fuse/detail/902) 与 [通信主题](https://iot.10086.cn/doc/aiot/fuse/detail/920)。

| 主题 | 方向 | 当前固件用途 |
|---|---|---|
| `P/post` | 设备 → 平台 | 上报属性，属性值带 `value` 包装 |
| `P/post/reply` | 平台 → 设备 | 平台上报确认；注意这里是 `/post/reply` |
| `P/set` | 平台 → 设备 | 设置一组闹钟，`params` 内为原始 Struct，不带 `value` |
| `P/set_reply` | 设备 → 平台 | 设置结果；注意这里是 `_reply` |
| `P/get` | 平台 → 设备 | `params` 为属性标识符数组 |
| `P/get_reply` | 设备 → 平台 | `data` 内为原始属性值，不带 `value` |

设备订阅 `set`、`get`、`post/reply`，订阅 QoS 为 1；当前发布使用 QoS 0、retain=false。控制端不要发布保留的旧设置指令。当前未实现属性期望值、远程校时、远程停止/贪睡或其他服务调用；仍用本地按键停止、贪睡和校时。

上报示例，外层每个属性都有 `value`：

```json
{"id":"101","version":"1.0","params":{"temperature":{"value":23.50}}}
```

闹钟上报中，完整 Struct 放在 `value` 里面，例如 `"alarm1":{"value":{"enabled":false,...}}`；这里的省略号只表示说明，不是可发送的 JSON。

GET 请求示例，不依赖传感器有效：

```json
{"id":"1002","version":"1.0","params":["alarm1","config_revision","sensor_valid"]}
```

GET 成功响应形状，`alarm1` 会返回完整 9 个成员：

```json
{
  "id": "1002",
  "code": 200,
  "msg": "success",
  "data": {
    "alarm1": {
      "enabled": false,
      "hour": 7,
      "minute": 30,
      "repeat_mask": 31,
      "year": 2026,
      "month": 11,
      "day": 13,
      "ring_seconds": 60,
      "snooze_seconds": 300
    },
    "config_revision": 2,
    "sensor_valid": false
  }
}
```

示例存储代数 `2` 只表示格式，实际值以设备为准。GET 数组也不能有重复项、未知项或空列表。OneJSON 编解码的单条负载上限为 **1460 字节**；日常按一组设置、小批量读取即可。

## 6. 错误、超时和重复请求

| 回复 | `msg` 示例 | 应如何处理 |
|---|---|---|
| `400` | `invalid_alarm_request` / `invalid_get_request` | 核对标识符、成员、类型、范围、日期、ID 与单组限制；不会部分保存 |
| `408` | `request_expired` | 新设置在 MCU 接收队列中已经老化至少 2500 ms；本次不提交保存，恢复网络后核对并重新操作 |
| `409` | `busy_or_conflict` / `busy` | 本机编辑、已有保存、版本竞争或缓存暂忙；等待当前操作结束，先读实际值再提交 |
| `409` | `request_id_conflict` | 缓存中相同 ID 对应另一份明确成员/取值；新的操作使用新 ID |
| `500` | `save_failed` | EEPROM 保存、验证或应用失败；未把请求配置作为成功应用，保留原运行配置，恢复后核对再重试 |
| `500` | `result_unknown` | 保存结果无法确认；先 GET 或读本机实际配置，不把它当成功或自动重复写入 |
| `503` | `alarm_unavailable` | 闹钟服务尚不可用 |
| `503` | `sensor_unavailable` | GET 包含温湿度，但当前没有有效测量；可另读闹钟和有效标志 |
| `413` | `reply_too_large` | GET 响应无法在编码容量内生成；减少读取项 |

非法 JSON、超限或缺少可恢复的合法 ID 时，设备可能直接丢弃，无法保证回复。平台也可能在指令到达设备前拒绝无效参数；控制台显示的“发送失败”不一定来自上表设备业务码。

OneNET 属性设置/获取的响应时限为 **5 秒**，见 [官方协议说明](https://iot.10086.cn/doc/aiot/fuse/detail/902)。当前实现通过优先处理命令/保存回复、串行网络工作任务、短天气请求和过期拒绝来降低延迟；**在网络故障、DNS/TLS 迟滞、ESP 恢复或多事务阻塞时，不能保证 5 秒内完成端到端往返**。

配置了 OneNET 后，天气改用 `AT+HTTPCGET`：模块网络超时参数为 1000 ms，主机等待该应答为 2000 ms。该参数不等于整条指令的硬实时上限；DNS、重试、恢复与其他共享 AT 操作仍可能带来更长延迟。新设置在 MCU 队列中老化达到 2500 ms 时回复 `408`，这个时间不是从云端点击时刻开始计量。

控制台超时后，配置有可能已经持久化、只是回复丢失。先重新读取 `alarmN`、`config_revision`，并查看保存日志；不要连续点击重发。EEPROM 最终提交后的 ACK 或回读丢失也有类似不确定性：本次返回失败、RAM 保持旧配置，但重新上电或下一次严格读盘可能发现已提交的内容，必须读回核对。

### 缓存边界

同一次 MCU 启动期间，云端服务保留 **4 个已接收命令条目**。在条目仍保留时，相同 ID、相同明确字段和值的重复请求不会重复排队写 EEPROM；完成后重发已缓存结果。相同 ID 改成不同设置返回冲突。已缓存失败结果的同 ID 重发仍是原结果；故障恢复后的新尝试先核对配置，再使用新 ID。

闹钟服务另保留 **最近 8 个远程保存完成结果**，当前 pending ID 单独保留，供网页与 MQTT 查询；更老的结果会变为 unknown。这不把云端去重容量扩大到 8。

这些缓存均在 RAM，MCU 复位、断电或条目被替换后不再保证识别旧指令；它们不是跨重启、跨断电的“恰好一次”执行日志。普通 MQTT 掉线重连后的行为也应结合“同次启动、条目尚在”的前提检查。新一次用户操作应使用新消息 ID。

## 7. 实板验收顺序

先做前 4 项；正常后再分次做其余项目，不要用无间隔反复保存来制造压力。

| 测试 | 操作 | 验收结果 |
|---|---|---|
| 在线与模型 | 按第 3 节观察日志与 OneNET 设备属性 | 在线、已订阅；有 `Cloud accepted report`；标识符和更新时间正确 |
| 单组完整与部分修改 | 完整设置 `alarm1`，再只改 `enabled`；对照本地列表/网页/GET | 保存 `200` 后值一致；其他成员及其余组保持 |
| 第 8 组独立 | 为 `alarm8` 设置另一时间和重复掩码 | `alarm1` 与 `alarm8` 独立；云端并没有第 9 组 |
| EEPROM 保持 | 等保存成功，先 MCU RESET，再断主电重启 | 已确认配置恢复；重新连接后属性一致 |
| 定时与本地响应 | 用设备实际日期，将一组设为未来 2…3 分钟、响铃 10 秒、贪睡 60 秒 | 到点响铃，K1 停止、K2 贪睡；保存与云端轮询不阻塞按键 |
| 云端与本机编辑 | 本地进入闹钟编辑/校时后发云端设置；再退出并重试 | 编辑期间拒绝忙状态；列表页面本身不等于编辑锁；退出、读回后可保存 |
| 云端与局域网页 | 同时在网页和云端保存，逐次读取结果；网页保存后紧接云端保存 | 不静默覆盖并发草稿；冲突需刷新；较早网页结果在最近 8 条保留范围内仍可查询 |
| 正常重复请求 | 有条件的控制台/API 调试工具以同 ID 重发相同指令，再以同 ID 改值 | 缓存范围内只执行一次保存；原结果可重发；改值冲突。控制台若每次自动换 ID，不能据此声称测过重复 ID |
| 非法设置 | 测试小时 24、贪睡 61、无效日期、只读温度写入、两组同时设置 | 平台或设备明确拒绝；实际配置、存储代数没有部分改变 |
| 传感器失败与恢复 | 通过已有测试夹具模拟 AHT20 不可用后恢复，不带电改接焊接板 | `sensor_valid:false`、无伪造新温湿度；闹钟 GET 正常；含温湿度 GET 为 `503`；恢复后测量更新 |
| AP/互联网掉线 | 云端提交附近中断网络再恢复，避免连续重发 | 本地时钟、到点响铃和 K1/K2 继续；网页取决于 LAN 是否仍可达；重连后读回最终配置 |
| 网络慢与天气失败 | 让天气服务不可达/超时，同时在可达的 MQTT 链路发设置 | 记录实际时延；可能出现 `408` 或平台超时；本地闹钟持续；故障恢复后可重连与重新操作，不将其记为“故障下也满足 5 秒” |
| MCU 单独复位 | ESP 保持供电，按 MCU RESET | AT 状态恢复、重新校时/连接与订阅；网页重新访问；旧云端 ID 不享有跨复位去重保证 |
| ESP 单独复位 | MCU 正常运行时复位 ESP | 本地功能持续；网络重新初始化、SNTP、TLS、订阅与属性补报恢复 |
| EEPROM 故障/恢复 | 用既有夹具模拟读失败、写失败或写保护，再恢复 | 失败不发成功 `200`；旧运行配置保持；恢复后新请求可保存，无需先在本地“解锁” |
| 启动读失败后的显式关闭 | 先存启用状态，夹具使启动读失败；恢复后云端只发 `enabled:false` | 成功后实际关闭；同组未下发的时间、日期、响铃/贪睡及其他 7 组保持存储值 |
| 保存中断与结果丢失 | 夹具/供电控制下模拟写入途中断电、提交后回复丢失，恢复并 GET | 完整旧/新日志恢复，不接受半份配置；没有回复即先核对，不能推断未执行或自动连发 |
| 日常回归 | 连网/断网、进出菜单、网页查看、云端读取，持续运行数小时 | RTC 日期星期、南京天气、温湿度、8 组闹钟及 `Workqueue running` 继续；无卡屏、HardFault 或异常堆/栈下降 |

故障测试沿用现有测试夹具，不带电短接 I2C、修改 H1 或擦写 ESP 固件。若没有能控制最终 EEPROM ACK/回读的夹具，只记录未完成该项，不把主机故障注入当作实板结果。

## 8. 按日志排查

| 日志 / 现象 | 先检查 |
|---|---|
| `Token missing or configuration invalid; local functions continue` | 是否完成本地 Token 流程、重新编译并烧录；公开 host/port 是否仍为 TLS `8883` |
| `Waiting for ESP SNTP before verified TLS` | WiFi、DNS、SNTP 可达性及实际时间；仅手动设置 STM32 RTC 不等于 ESP 已校时 |
| `Token expired; regenerate local Token, rebuild and flash` | 本地重新生成 Token，构建并烧录；不要打印密码排查 |
| `TLS/credential configuration failed` | 当次 ESP 版本、CA 读写/回读命令与长密码命令支持、SNI 设置；保留脱敏日志 |
| `MQTT connection failed` | 服务地址、端口、路由、时间、Token 有效性和平台设备状态；不要将证书校验改为关闭 |
| `MQTT subscription failed` | 产品/设备和订阅主题权限，AT 是否失步；随后应由原网络恢复机制重试 |
| `MQTT disconnected; reconnect pending` / `retry pending` | 网络恢复后等待重连；失败退避逐步延长，最长 60 秒，成功连接后恢复初始节奏 |
| `Cloud rejected report ... mask ... code ...` | OneNET 属性标识符、Struct 成员、类型、读写权限和取值范围；单属性被拒会冷却约 30 秒再试，其他可用属性可继续 |
| `Property report not confirmed; retry pending` | 已发上报约 10 秒仍无平台业务确认；检查 `post/reply`、网络和平台记录，不能当成已接受 |
| 云端显示旧值 | 对照上报时间、`sensor_valid`、GET 与本机列表；区分平台保留值、运行 RAM 与已确认存储 |
| 网页暂时慢 | 网页、MQTT、SNTP、天气共用原网络 worker；观察 AT 恢复与慢事务，避免同时连点保存 |

需要协助时，提供当前版本、`AT+GMR` 信息、从 WiFi/SNTP 到 MQTT 的脱敏日志、相关消息 ID、指令成员、回复码，以及操作前后 GET/板端值即可。不要提供设备密钥或 Token。

日志中的 `transport error` 是本工程错误枚举：`1` 参数、`2` 传输/超时、`3` 命令拒绝、`4` CA 存储/回读、`5` 接收溢出、`6` 断开；它不是 ESP 原始错误码。上报拒绝日志的 `mask` 中 bit0…7 对应 alarm1…8，bit8 温度、bit9 湿度、bit10 sensor_valid、bit11 config_revision、bit12 ringing_mask。

## 9. 本次代码验证记录

2026-11-13 的 v1.4 基线：完整 `tests/run_host_tests.ps1` 返回 0，包含 OneJSON 204 检查、应用 11 场景、闹钟引擎 297 检查、EEPROM 937 场景、闹钟服务 41 场景、ESP 两种复位状态及原有显示、网页、RTC、AHT20、日志与硬件替身回归。主机测试使用公开虚构凭据，不使用真实设备认证。

v1.4.1 增量：ESP 解析、两种上电状态全套传输回归及 OneNET 服务测试通过，包括首次 CLEAN 未初始化、其他错误不放行、原始错误码解析、后续各配置步骤失败和日志不含测试 Token。Debug/Release 再次编译链接通过，无警告或错误。

ARM GCC 14.3.1 编译结果如下，最终编译日志无警告或错误：

| 构建 | Flash 字节 | 主 SRAM 字节 | CCM 字节 |
|---|---:|---:|---:|
| Debug 完整时钟 v1.4.3 | 308012 | 116600 | 15900 |
| Release 完整时钟 v1.4.3 | 259204 | 116592 | 15900 |
| 独立 RGB 诊断（v1.4 基线） | 51720 | 100472 | 5028 |
| 独立有源蜂鸣器（v1.4 基线） | 303608 | 116512 | 15900 |

Debug/Release 默认目录为完整时钟、PWM 蜂鸣器，两个开关均为 OFF。Release BIN 从 `0x08000000` 起共 259204 字节。ELF 检查确认 ARM 格式及 NOLOAD CCM 缓冲，初始化先于任务接收；网络栈仍为 8 KiB。主 SRAM 链接占用约 88.96%，须继续观察实板日志的历史最小堆和栈余量。

v1.4.1 的增量证据为同目录 `cleanup-red.log`（修复前复现失败）、`cleanup-green.log`、`cleanup-service.log`、`cleanup-debug.log`、`cleanup-release.log`。

v1.4.2 增量：测试先观察旧驱动发送不匹配 SNI、旧服务未选择专用 CA 策略而失败，再修复通过。ESP 两种复位状态全套传输回归与 OneNET 服务测试通过，覆盖省略 SNI、默认主机名、显式名称及非法名称；Debug/Release 编译链接无警告。证据 `sni-red.log`、`sni-service-red.log`、`sni-green.log`、`sni-service-green.log`、`sni-Debug.log`、`sni-Release.log`。

v1.4.3 增量：按实际库格式的 quoted-port 测试先复现失败，再修复通过；ESP parser 和两种复位状态传输回归通过。新增状态4/5/6 正例及错端口、非法数字、缺引号、空端口、错主机、scheme2、断连状态拒绝用例，旧无引号格式仍通过。独立只读审查无具体问题；Debug/Release 编译无警告。证据 `state-port-red.log`、`state-port-green.log`、`state-port-Debug.log`、`state-port-Release.log`。

完整主机日志和最终构建日志在 `build/onenet-validation/`：`host-tests.log`、`service-final.log`、`debug-rebuild.log`、`debug-fresh.log`、`release-final.log`、`rgb-final.log`、`active-buzzer-final.log`。原 Debug ELF 被其他进程占用而不能删除，保留该进程并完整重编对象；另用全新 Debug 目录完成配置和编译验证。

本次未自动烧录开发板、未使用真实凭据连接云端，也未提交 Git。第 7 节实板项目仍须逐项操作并留存结果。
