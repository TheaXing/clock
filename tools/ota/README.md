# 电脑端 OTA 工具

需要 Node.js 20+，只使用内置库。在仓库根目录通过 PowerShell 执行。工具不烧录芯片，不修改 EEPROM 或 MCU 选项字节。

## 生成本地配置

```powershell
node tools/ota/ota.mjs init --config app/ota_config_local.h --key-file app/ota_key_local.json --host 192.168.1.10 --port 8788 --path /firmware.ota --version 1
```

将 `--host` 改为电脑在设备所在局域网中的固定 IPv4 地址或可解析主机名；受 ESP 连接命令限制，最长 64 个 ASCII 字节。默认值为 `192.168.1.10:8788/firmware.ota`，版本为 1。`--config` 必须显式指定；`--key-file` 可选，建议同时生成，供后续打包和服务器验证使用。输出目录须已存在；任何输出已存在时均拒绝覆盖，不打印秘密。

自动更新使用固定包查询，基础 `--path` 最长 156 个 ASCII 字节。设备和服务器必须配置相同的主机、端口及基础路径；`0.0.0.0` 只用于服务器监听，不能填写为设备下载地址。

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

只读服务提供固定包路径及两种严格查询：`?latest=1` 返回公开发现信息，`?id=<packageId>` 只在身份匹配当前冻结包时提供该包；未知身份及其他查询返回 404。保留基础路径作为兼容二进制别名。不提供路径遍历、目录浏览、写入或代理。GET/HEAD 无 Range 返回 200；单 Range 返回 206，包含准确 `Content-Length`、`Content-Range` 与完整包 SHA-256 形成的强 ETag。提供 `If-Match` 时必须精确匹配该强 ETag，否则返回 412；无效、多段或越界 Range 返回 416。支持闭区间、开放结尾和后缀 Range，闭区间结尾超过文件时按 HTTP 规则截到文件尾。不压缩、不分块编码，每次响应关闭连接。设备使用显式 Range，并在后续块携带首次得到的 ETag。

## 上传与发布后台

长期使用后台保存历史包，无需每次停服切包：

```powershell
.\scripts\start_ota_server.ps1
# 等效的直接 Node 命令：
node tools/ota/ota.mjs admin --key-file app/ota_key_local.json --data-dir .ota-data --host 0.0.0.0 --port 8788 --path /firmware.ota
```

后台需要已存在的实际密钥 JSON，读取 `keyHex` 验证包和 `adminTokenHex` 认证管理请求，不会生成或覆盖密钥。默认将版本库保存在被 Git 忽略的 `.ota-data/`；不要把持久数据放入可被清理的 `build/`。在一台电脑上只启动一个服务进程管理同一数据目录。可以用 `--data-dir` 指定持久目录、`--max-bytes` 指定包存储配额，默认 `268435456`（256 MiB）。不要手动修改已接收的包或记录；后台会拒绝不一致的对象，损坏的当前指针会使重启失败，不会把暂存包自行设为发布。

浏览器打开 `http://<电脑实际局域网IP>:8788/`，使用 JSON 的 `adminTokenHex` 登录。令牌只保留在当前页面内存，退出或刷新后重新输入。不要输入 `keyHex`。页面无外部字体、脚本或 npm 依赖。

1. 构建新版本，例如 `.\scripts\build_ota.ps1 -Version 2 -Configuration Release`，得到认证 `.ota`。
2. 在后台点击或拖入 `.ota`；进度完成后，服务端验证包头 HMAC、板型、长度、向量和镜像摘要。版本来自包头，无需网页手填。只有通过验证的包会出现在版本库中。
3. 上传完成只是**暂存**。在版本列表选择“发布此版本”，核对版本及包身份后点击“确认发布”。当前发布卡片更新后，设备才可发现该版本。
4. 保持后台进程和电脑联网运行，让装有新协议 Bootloader 的设备正常开机。后台显示“已发布”表示服务端已完成切换，不代表设备已经安装。

每份上传最多 `458752` 字节（448 KiB），最多两个上传并发，每个请求有 30 秒完整传输期限。要求准确 `Content-Length` 和 `application/octet-stream`，不接受压缩或分块上传。包文件名不参与存储路径。同版本、同包可重复上传；同版本对应不同包返回 409，需要递增版本重构建。达到配额时拒绝上传，保留已接收版本；没有删除版本接口。配额包含正在接收的完整预留字节，因此配额已满时重复上传同包也可能返回507；服务端不永久缓存完整历史包。

后台列表同时显示当前发布和暂存版本。允许手动回切已验证旧版本供人工恢复；自动设备只升级到更高版本，已开始安装的目标不会因发布切换而改变。历史包持续保留供固定身份下载和中断恢复。

### HTTP 接口

管理请求使用 `Authorization: Bearer <64位十六进制令牌>`，不使用 cookie 或 URL 令牌。携带 `Origin` 时必须与本服务 HTTP 地址完全同源，不开放 CORS。静态资源仅允许 `/`、`/app.mjs`、`/style.css`。

| 接口 | 认证与返回 |
| --- | --- |
| `GET /api/releases` | 管理认证；`{releases,current,storage:{usedBytes,limitBytes}}`，无发布时 `current:null` |
| `POST /api/releases` | 管理认证；原始 `.ota` 正文，新增返回 201，重复返回 200；`{release,created}` |
| `POST /api/releases/<packageId>/publish` | 管理认证；空正文，原子发布后返回 `{release}`（含 `publishedAt`） |
| `GET /firmware.ota?latest=1` | 公开发现；无发布 `{"available":false}`，有发布包含 `available:true,version,packageId,header` |
| `GET/HEAD /firmware.ota?id=<packageId>` | 公开的不可变认证包，支持 Range、强 ETag、精确 If-Match |
| `GET/HEAD /firmware.ota` | 当前发布包的兼容二进制别名，无发布返回 404 |

`release` 包含 `packageId`、`version`、`imageSize`、`packageSize`、`packageSha256`、`imageSha256`、`board`、`uploadedAt` 等公开字段。发现正文少于 1024 字节：`packageId` 是 32 个小写十六进制字符，`header` 是原始 256 字节认证包头的 512 字符小写十六进制；不提供任意下载主机或 URL。基础路径自定义时上述固件接口随之改变。查询只接受精确的 `latest=1` 或单独 `id=<packageId>`，拒绝额外参数或编码别名。

对象按完整包 SHA-256 存盘，验证并同步完成后才创建版本记录；当前发布指针独立同步并原子重命名。发布失败保留旧指针，重启重新验证版本记录及指针；没有发布的上传不会因重启自动发布。每次从磁盘读取包时重新校验身份和摘要，不用损坏的新字节冒充旧 ETag。

HTTP 错误包括：401 令牌错误、403 来源错误、409 版本冲突、411 缺少长度、413 过大、415 类型/编码错误、422 认证或镜像无效、429 上传并发已满、408 上传超时、507 配额不足、503 存储或完整性异常。首版用于电脑／局域网 HTTP，不提供公网托管。

## 自动测试

```powershell
node --test tools/ota/test_ota.mjs
node --test tools/ota/test_admin.mjs
node tools/ota/ota.mjs --help
```

测试使用固定公开测试密钥和临时目录，包括独立包 fixture、逐字节篡改、重新认证后的非法字段、边界向量、工厂 HEX 校验和与地址、Range/ETag/If-Match、固定路径、内存快照、CLI 输出保护及秘密不回显。通过主机测试不能证明实板 ESP 下载、Flash 安装或掉电恢复已通过。

后台测试使用真实临时文件系统和真实本地 HTTP，包括认证/同源、暂存/发布分离、无效包、超限/慢速/中断上传、并发限制、配额预留、重复版本冲突、发布与版本记录重命名故障、重启持久化、磁盘损坏拒绝、发布切换后的固定 Range 下载、静态白名单及 CLI 启停。不读取或修改用户实际凭据。
