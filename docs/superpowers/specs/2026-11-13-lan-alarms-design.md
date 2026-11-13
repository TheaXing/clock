# 第二阶段：同一 WiFi 的手机闹钟网页

用户已授权直接实现并编译，第一阶段已通过实板验证；H1 保持用户现有焊接状态，不改 GPIO、蜂鸣器或按键线路。继续在当前工程和现有开发分支实现，保留全部未提交工作，不另建不含现有文件的 worktree，不提交、烧录或刷 ESP 固件。

目标：手机与 ESP32C3 连同一 WiFi，通过浏览器访问串口打印的局域网 HTTP 地址，查看、编辑、开关全部 8 组闹钟；只有 EEPROM 提交且回读成功、运行配置已应用后，网页才显示保存成功。离线本地时钟、南京天气、AHT20、按键、响铃/贪睡保留。

方案：沿用现有 ESP-AT，使用多连接 TCP server 和被动接收；STM32 提供自包含中文手机网页和有限 HTTP API。替代方案是改写 ESP 原生固件或增加服务器/MQTT，前者影响已验证 AT 天气链路，后者超出用户已选的同网网页范围，均不采用。AT 命令参考 Espressif 官方 ESP32C3 TCP/IP 文档；旧 CIPRECVMODE 与新 CIPRECVTYPE 做兼容。网页存 Flash，不下载 CDN/字体，不依赖互联网或 App。

ESP UART 仍只有原 network 任务发命令。ISR 区分 unsolicited +IPD/CONNECT/CLOSED 与命令结果，长度帧数据里的 OK/ERROR 不视作 ACK。被动接收使用 256 字节块；最多 5 个连接有独立解析与 generation，关闭/复用后的旧响应不得写到新客户端。CIPSEND 识别提示符和 SEND OK，所有等待有限。模块复位/串口失步恢复原 AT 初始化；缺少可选 server 命令不停止时钟或天气。

HTTP：GET `/` 返回内置网页；GET `/api/alarms` 读实际运行配置、版本、RTC/保存/响铃状态；POST `/api/alarms/1`…`8` 接收完整 URL-encoded 字段及版本/session，必须有 `X-Clock-Request: 1`、正确表单类型与 Content-Length；GET `/api/result?id=N` 查询异步保存结果。每连接一请求后关闭；限制行长、累计 header 和 body，拒绝 chunked、重复/矛盾长度、非法数字/日期/星期、未知字段。无 CORS 放行，不反射请求到 HTML。

跨任务：remote submit 将复制的请求交给闹钟 owner；owner 再校验版本、忙状态和本地编辑冲突。唯一 EEPROM worker 使用已验证的严格基线与字段合并，写入成功后 owner 应用完整配置。HTTP 202 只表示接受排队，网页轮询 saved/failed；通信中断的结果不确定时刷新实际状态，不自动重发保存。并行手机或本地修改导致旧版本冲突，不静默覆盖。会话标识区分网页服务重启，属于并发控制，不作为账号认证。

页面：8 张卡片，启用开关、时间、每周星期/指定日期、响铃时长及贪睡时长；每组独立保存。中文状态区显示连接、正在保存、已保存、失败、配置已更新。编辑时暂停自动刷新，保留草稿；空闲约 10 秒刷新。单次网络等待允许覆盖已有最长约 20 秒天气事务，超时给出核对提示。

验证：保留原全部主机测试；新增 HTTP 分包/边界/恶意输入、ESP server 实际收发时序、异步保存/EEPROM 失败/并发冲突测试；完整 Debug/Release、RGB 与有源蜂鸣器编译，检查 RAM/Flash。真实手机、AT 固件能力、路由器隔离、页面往返/掉线/EEPROM 断电由实板步骤验收。
