# OneNET 闹钟控制页面

通过 OneNET 云端读取开发板上的 8 组闹钟，并下发单组闹钟设置。适配当前 `v1.4.3-ONENET` 固件，无需为此重新烧录。

## 启动

需要 Node.js 20 或更新版本，不需要安装 npm 依赖。在工程根目录运行：

```powershell
node tools/onenet-alarm-control/server.mjs
```

在浏览器打开 [http://127.0.0.1:8787/](http://127.0.0.1:8787/)。本地服务只监听当前电脑的回环地址；开发板可以通过其他网络连接 OneNET。

如果端口被占用，可以在 PowerShell 设置专用端口后启动：

```powershell
$env:CLOCK_ALARM_CONTROL_PORT = '8788'
node tools/onenet-alarm-control/server.mjs
```

结束时，在运行服务的终端按 `Ctrl+C`。直接双击 `index.html` 可以看到页面，但模块与云端调用需要通过上述本地服务加载。

## 使用

1. 确认开发板已连接 OneNET，物模型包含 `alarm1` 到 `alarm8`，每组包含当前固件的 9 个成员。
2. 产品 ID 和设备名称已预填为 `4lh76atTpR`、`clock001`。
3. 在页面填写可调用设备属性 API 的 **用户级 API Token（推荐）或有相应权限的产品级 API Token**。如果没有 Token，展开“没有 API Token？在本机生成”，输入用户 ID 和用户 AccessKey，点击生成即可自动填入，有效期 1 小时。用户 ID 在 DMP 控制台首页右上角查看，用户 AccessKey 在个人“账号信息 / 访问权限”中查看；界面版本不同，菜单名称可能略有差异。生成过程只在浏览器 WebCrypto 中计算，AccessKey 不发送给本地服务或 OneNET，并在生成后清空。使用 OneNET 北向 API 的 `2022-05-01` 版鉴权；设备 MQTT 密钥、设备 `2018-10-31` 版 Token 不适用于这里。详见 [OneNET REST 安全鉴权](https://iot.10086.cn/doc/aiot/fuse/detail/1464)。设备绑定项目时，使用有项目权限的用户级 Token。
4. 点击“读取开发板”，获取设备当前配置。卡片中的时间来自实际读回或已确认保存的设置；尚未读取的组显示空值。
5. 选择一组闹钟，设置启停、时间、重复星期或指定日期、响铃时长和贪睡间隔。所有时间按开发板 **UTC+8** 理解。
6. 点击“下发并保存闹钟”。只有 OneNET API 返回 `code: 0` 且设备回复 `data.code: 200` 时才显示“开发板已确认保存”。

一次仅修改一组闹钟，符合当前固件的限制。其他 7 组保持原值；切换组号会保留当前页面中尚未下发的草稿。

如果出现超时或网络中断，指令可能已到达设备。页面会标记“保存结果尚未确认”，不会自动重发。先点击“读取开发板核对”；读回相同值表示配置一致，只有设备保存回复 `200` 才被标记为这次指令已确认成功。

## 文件与接口

- `index.html`：页面和响应式样式。
- `app.mjs`：读取、编辑、发送和结果显示。
- `model.mjs`：参数校验、OneNET 请求和回复判定。
- `server.mjs`：零依赖的同源本地转发服务，规避浏览器跨域限制。

本地服务只转发到固定的 OneNET HTTPS 地址：

- [主动读取属性](https://iot.10086.cn/doc/aiot/fuse/detail/1419)：`POST /thingmodel/query-device-property-detail`。
- [设置设备属性](https://iot.10086.cn/doc/aiot/fuse/detail/1418)：`POST /thingmodel/set-device-property`。

API Token 只保留在当前页面，不写入源码、磁盘或浏览器存储；关闭或刷新页面后需重新填写。本地服务不打印 Token 或请求正文，也不使用开发板的 MQTT Client ID 建立第二个连接。

## 验证

本机测试继续放在已被 Git 忽略的根 `tests/` 目录中：

```powershell
node --test tests/onenet_control_model_test.mjs tests/onenet_control_server_test.mjs tests/onenet_control_app_test.mjs
```

测试使用公开虚构 Token 和替身上游，不会向真实开发板发送闹钟指令。真实下发需要在页面填写有权限的 API Token 后操作。
