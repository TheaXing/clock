# 电脑端 OTA 工具

需要 Node.js 20+，只使用内置库。在仓库根目录通过 PowerShell 执行。工具不烧录芯片，不修改 EEPROM 或 MCU 选项字节。

## 生成本地配置

```powershell
node tools/ota/ota.mjs init --config app/ota_config_local.h --key-file app/ota_key_local.json --host 192.168.1.10 --port 8788 --path /firmware.ota --version 1
```

将 `--host` 改为电脑在设备所在局域网中的固定 IPv4 地址或可解析主机名；受 ESP 连接命令限制，最长 64 个 ASCII 字节。默认值为 `192.168.1.10:8788/firmware.ota`，版本为 1。`--config` 必须显式指定；`--key-file` 可选，建议同时生成，供后续打包和服务器验证使用。输出目录须已存在；任何输出已存在时均拒绝覆盖，不打印秘密。

配置头使用 `CLOCK_OTA_HOST`、`CLOCK_OTA_PORT`、`CLOCK_OTA_PATH`、`CLOCK_OTA_KEY_HEX`、`CLOCK_OTA_ADMIN_TOKEN_HEX`、`CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX`、`CLOCK_OTA_VERSION`。发布密钥与网页管理令牌分别生成 32 字节随机值。`CLOCK_OTA_ADMIN_TOKEN_SHA256_HEX` 是对十六进制令牌解码后的 32 字节计算的 SHA-256，不是对其 64 字符文本计算摘要。App 只使用此校验摘要，不嵌入原始令牌；发布 HMAC 密钥只进入 Bootloader，不进入可下载的 App BIN。原始令牌宏保留用于源码兼容，JSON 的 `adminTokenHex` 仍供操作者在网页输入，`keyHex` 用于打包认证。

旧版配置头没有令牌摘要宏时，网页触发会被禁用。迁移时可用原来的管理令牌计算并补入摘要宏，保留既有发布密钥和令牌；也可通过 `init` 向新的显式路径生成完整配置后安排重新配置。`init` 始终拒绝覆盖旧配置。请私下保管配置和 JSON，不提交到 Git，不把 JSON 放入网页目录。默认本地配置文件已在根 `.gitignore` 中忽略；使用自定义路径时自行确保不提交。

## 打包与首次安装镜像

先使用项目的 OTA 配置构建 Bootloader 和重定位 App。App 必须从 `0x08010000` 链接；旧版从 `0x08000000` 链接的 BIN 不能用于无线包。

```powershell
node tools/ota/ota.mjs pack --app build/OtaRelease/Clock_CubeMX.bin --boot build/OtaRelease/Clock_Bootloader.bin --key-file app/ota_key_local.json --version 2 --output build/OtaRelease/clock-v2.ota --factory-output build/OtaRelease/clock-factory-v2.hex --report build/OtaRelease/clock-v2-report.json
```

实际 BIN 路径以构建输出为准。所有输入和输出路径都可指定；已有输出拒绝覆盖。`--boot` 与 `--factory-output` 必须同时提供，不需要工厂镜像时同时省略。`--report` 可选，控制台始终输出不含秘密的版本、大小、摘要和包身份。版本必须为 `1..4294967295`，允许经过认证的较旧版本。密钥文件缺失、格式错误或全零时立即失败。

`.ota` 为精确的 256 字节小端包头和原始 App BIN；HMAC-SHA256 认证头前 192 字节，头内 SHA-256 绑定全部镜像。打包同时检查长度、目标板、向量栈对齐与 SRAM 范围、Thumb 复位地址及其精确镜像范围。BIN 没有地址元数据，重定位检查通过向量中实际复位地址完成，不能替代 ELF 链接布局检查。

工厂 Intel HEX 只含三段有效数据：Bootloader 于 `0x08000000`，App 于 `0x08010000`，带提交标记的认证安装描述于 `0x0807FF00`。不向空隙补数据，不包含 EEPROM 数据。**首次 SWD 安装使用组合工厂 HEX**；单独刷入重定位 App 不能建立 Bootloader 启动入口。后续无线发布只用 `.ota`。

## 只读局域网服务器

```powershell
node tools/ota/ota.mjs serve --input build/OtaRelease/clock-v2.ota --key-file app/ota_key_local.json --host 0.0.0.0 --port 8788 --path /firmware.ota
```

允许电脑防火墙的相应局域网入站端口，并保持服务运行。默认监听所有 IPv4 接口；设备配置使用电脑的实际局域网地址，不能使用 `0.0.0.0`。服务器启动时完整验证包并复制到私有内存；替换或删除磁盘文件都不会改变当前发布内容。切换版本需停止后重新启动服务。终端记录完成请求的 HTTP 状态与公开数据范围，便于结合设备串口观察恢复下载，不记录请求头或管理令牌。

仅提供固定路径，不支持查询参数、路径遍历、目录浏览、写入或代理。GET/HEAD 无 Range 返回 200；单 Range 返回 206，包含准确 `Content-Length`、`Content-Range` 与完整包 SHA-256 形成的强 ETag。提供 `If-Match` 时必须精确匹配该强 ETag，否则返回 412；无效、多段或越界 Range 返回 416。支持闭区间、开放结尾和后缀 Range，闭区间结尾超过文件时按 HTTP 规则截到文件尾。不压缩、不分块编码，每次响应关闭连接。设备使用显式 Range，并在后续块携带首次得到的 ETag。

## 自动测试

```powershell
node --test tools/ota/test_ota.mjs
node tools/ota/ota.mjs --help
```

测试使用固定公开测试密钥和临时目录，包括独立包 fixture、逐字节篡改、重新认证后的非法字段、边界向量、工厂 HEX 校验和与地址、Range/ETag/If-Match、固定路径、内存快照、CLI 输出保护及秘密不回显。通过主机测试不能证明实板 ESP 下载、Flash 安装或掉电恢复已通过。
