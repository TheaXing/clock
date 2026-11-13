# Clock_CubeMX 移植与上板验收

当前版本为 v1.1-HAL，恢复旧工程的时钟、温湿度、WiFi、SNTP、南京实时天气和页面显示。启动依次显示欢迎页和联网页；初次连接失败时短暂显示错误页，然后进入主页面。主任务启动后网络在后台重试，离线时本地时间和传感器任务继续运行。

源工程：`D:/code/Clock Project/11-1FreeRTOS - keil`。移植范围以其 Keil 实际编译清单和启动调用链为准：欢迎/联网/错误/主页面、RTC 日期星期及闪烁冒号、SNTP、WiFi 状态、AHT20 温湿度、实时天气与图标，以及实际使用的 8 套字体和 13 张图片。

保留当前验证过的 HAL SPI2 屏幕传输、RGB565 字节序、分块发送和 GCC Cortex-M4F FreeRTOS 移植层。未引入旧 SPL、第二套启动代码或 RTOS。当前支持构建入口为 CMake/GCC；EWARM 文件不是本次构建入口。

## 配置与编译

旧 WiFi 和天气设置已自动复制至 `app/clock_config_local.h`，该文件被 Git 忽略。修改其中的 `CLOCK_WIFI_SSID`、`CLOCK_WIFI_PASSWORD`、`CLOCK_WEATHER_KEY`、`CLOCK_WEATHER_LOCATION` 即可，不需要改驱动。提交或复制工程到另一台电脑时，需单独带上本地配置。缺少该文件时仍能编译，网络功能会提示配置缺失。旧密钥是否仍有效需实测。

在 PowerShell 中运行：

```powershell
.\scripts\build.ps1 -Configuration Debug -Clean
.\scripts\build.ps1 -Configuration Release -Clean
.\tests\run_host_tests.ps1
```

构建脚本会查找本机 STM32Cube 安装的 ARM GCC/Ninja；CMake 需在 PATH 中。主机测试使用 Visual Studio 2022 Community 的 C++ 编译工具，测试 UART/I2C/RTC/LCD 的替身不能替代物理硬件。

本次拆分后需重新执行完整主机回归、Debug/Release 和独立 RGB 构建，并以新生成的链接 map/size 输出记录内存用量。旧移植计划中的构建结果是当时的历史记录，不代表当前快照的验证或资源占用。主机测试覆盖天气解析、五个应用任务行为场景、页面/UI/渲染器、AHT20、RTC、串口日志及 ESP 传输。

烧录产物位于 `build/Debug/`、`build/Release/` 下的 `Clock_CubeMX.elf`、`.hex`、`.bin`。BIN 烧录起始地址为 `0x08000000`。本次工作只编译，不自动连接或烧录开发板。

需要回归原有红绿蓝测试时：

```powershell
.\scripts\build.ps1 -Configuration Debug -RgbTest
# 恢复完整时钟固件：
.\scripts\build.ps1 -Configuration Debug
```

普通时钟固件每约 10 秒通过原工作队列输出 `Workqueue running`，同时报告剩余/历史最小 FreeRTOS 堆和 clock/sensor/network 的栈余量（单位 word，1 word=4 字节）。RGB 模式保持原每秒工作队列日志。它们共用同一输出文件，烧录前确认最后一次构建的模式。

## 接线

| 功能 | MCU 引脚与参数 |
|---|---|
| 调试串口 USART1 | PA9 TX、PA10 RX；115200、8N1 |
| ESP USART2 | PA2 TX 接 ESP RX；PA3 RX 接 ESP TX；115200、8N1；共地 |
| AHT20 I2C2 | PB10 SCL、PB11 SDA；100 kHz；确认板上上拉 |
| LCD SPI2 | PB13 SCK、PC3 MOSI、PC2 MISO；当前已跑通配置 |
| LCD 控制 | PE2 CS、PE3 RST、PE4 DC、PE5 BL |
| RTC LSE | PC14/PC15，32.768 kHz 晶振；断主电保持需要有效 VBAT 供电 |

## 上板测试清单

1. **冷启动和显示**：打开 USART1 串口，确认 `[SYS]` 日志、欢迎页和主页面；检查中文星期、数字、星形图、WiFi 图、边缘像素与颜色。欢迎页后显示联网页；初次连接失败时短暂显示错误页，然后进入主页面并启动独立的时钟、传感器和网络任务，失败后继续离线运行。
2. **联网**：确认 AP 账号正确，ESP 支持 `AT+RST`、`AT+CWSTATE?`、`AT+CWJAP?`、`AT+CIPSNTPCFG`、`AT+CIPSNTPTIME?` 和 `AT+HTTPCLIENT`。初始化和串口失步恢复时通过 `AT+RST` 软重启模块、等待 `ready`，以排除旧命令迟到应答；保留模块持久配置，不恢复出厂设置。主页面显示实际 SSID；首次联网后出现 `[SNTP]` 和 `[WEATHER]` 日志。当前接口适配原工程所用 ESP AT 固件；不匹配时需查看模块固件版本。
3. **时间和保持**：核对 UTC+8 时间、日期、星期和冒号每秒变化。同步成功后暂时关闭 AP，按复位键，时间应继续而非重置；有 VBAT 时再做断主电保持测试。跨午夜、月末、闰年日期需单独确认。RTC/LSE 初始化失败会报告 `[RTC]`，仍继续显示和其他任务。
4. **温湿度**：每约 3 秒刷新，数值与参考温湿度计大体一致。拔下传感器应显示占位并持续重试；重新接入应恢复。确认温度负值/异常值不会留下随机字符。
5. **天气**：确认地点正确，室外温度和天气图标匹配，约每分钟查询一次。失效密钥、服务错误、网络超时期间保留最后一次有效天气；恢复后继续更新。检查旧天气账号的有效期和调用额度。
6. **断网和模块恢复**：运行中关闭 AP 或重启 ESP，确认本地时间、温湿度和日志仍运行；恢复后重新显示 SSID、同步时间并更新天气。单次连接/HTTPS 等待最多约 20 秒，只占用网络任务。
7. **持续运行**：至少运行数小时，观察堆历史最小值稳定、各任务栈有余量，无 HardFault、断言、卡屏或乱码。连续做 AP 开关、ESP 复位、传感器断开恢复。主机链接后的 SRAM 剩余同时供 newlib 动态堆使用，不能只看 FreeRTOS 堆余量。

## CubeMX 后续重新生成

`.ioc` 已补齐 USART2、I2C2 和 RTC/LSE。FreeRTOS 仍由顶层 CMake 手动接入；保持 SysTick/PendSV/SVC 属于 FreeRTOS，HAL 时基使用 TIM7，勿再生成重复 RTOS 内核/异常处理函数。系统时钟继续使用 HSE/PLL，PLLQ 保持 4。

RTC 的有限等待和保持日历逻辑位于 `Core/Src/rtc.c` 的 USER 区域，`RTC_Init 0` 会调用它后提前返回，以防生成器默认时间覆盖已有日历。**重新生成后检查 `SystemClock_Config()`：保留当前 HSE 系统启动，让 LSE 启动交给初始化任务中的 `rtc_init()`；不要新增遇到 LSE 失败就进入 `Error_Handler()` 的系统启动路径。** 如果生成器在 main 中增加直接 `MX_RTC_Init()`，移除这次重复调用，保留任务内入口。

中文导入源文件仍是 GBK，只有这些文件设置了对应 GCC 编译字符集；HAL/新应用文件保持 UTF-8。不要批量转换旧字库和页面的编码。字库仅包含旧工程已生成的汉字，新增中文需补字模。

时钟、温湿度、网络由独立任务处理；软件定时器服务中不执行显示或阻塞队列提交。LCD 由 UI 任务唯一操作；页面生产者只提交绘制消息，队列满时由任务等待以保持绘制顺序。ESP 事务由 network 任务顺序执行。日志统一经过 `clock_log/clock_log_line`，锁定整个格式化输出；新代码不要直接调用共享 stdout 的 printf/puts。独立的 snprintf 字符串格式化可以正常使用。

## v1.1-HAL：室内采集修正与南京天气

室内显示 `--` 且串口提示测量失败时，先核对采集协议。上一版沿用了旧驱动把 `0x71` 当作命令写入的做法；[Aosong AHT20 官方手册第 9 页](https://www.aosong.com/userfiles/files/media/AHT20%E4%BA%A7%E5%93%81%E6%89%8B%E5%86%8C%20A4%2020201222.pdf)中该字节表示 7 位地址 `0x38` 加读位。HAL 的 Master Receive 已自动发送此地址，现已删除多余写入，并在触发测量后至少等待 80 ms、校准命令后至少等待 10 ms。RTOS 延时加一个保护 tick，覆盖调用恰好靠近 tick 边界的情况。

重新烧录后确认启动日志包含当前版本 `v1.1-HAL`。正常每约 3 秒输出 `[AHT20] T=... C RH=... %`；失败日志包含 `stage`、`reason`、`HAL`、`I2C`、`status_valid` 和 `status`。

- `stage=initial-status`：初始状态读取；`calibration`/`calibration-status`：校准；`trigger`：启动测量；`wait-status`：等待转换；`data`：读取数据。
- `reason`：1 写传输失败、2 读传输失败、3 校准超时、4 测量超时、5 未校准、6 参数无效、7 传感器忙。
- `HAL`：0 成功、1 错误、2 忙、3 超时；`I2C` 是 HAL 原始错误位掩码，例如 `0x00000004` 表示 ACK 失败。只有 `status_valid=1` 时才解释传感器状态字节。

默认天气地点为南京城市 ID `WTSQQYHVQ973`；请核对本地配置，现场确认返回地点与预期一致。API 仍请求英文 JSON，页面将返回的 `Nanjing` 转为现有字库支持的“南京”。
