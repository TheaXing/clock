# STM32 天气时钟

本项目基于 STM32F407、C11 和 FreeRTOS，实现天气时钟、闹钟、网页控制及 OneNET 接入。本文介绍仓库根目录中的文件和目录，以及常用开发入口。


## 启动后台服务

以下命令在仓库根目录的 PowerShell 中运行，需要 Node.js 20 或更新版本，无需安装 npm 依赖。

### 闹钟配置后台

启动 OneNET 闹钟配置服务：

```powershell
node tools/onenet-alarm-control/server.mjs
```

打开 [http://127.0.0.1:8787/](http://127.0.0.1:8787/)。详细使用方法见 [控制工具说明](tools/onenet-alarm-control/README.md)。

### OTA 固件后台

先按照 [OTA 工具说明](tools/ota/README.md)生成本地密钥并打包固件，然后启动只读固件包服务器：

```powershell
node tools/ota/ota.mjs serve --input build/OtaRelease/clock-v2.ota --key-file app/ota_key_local.json --host 0.0.0.0 --port 8788 --path /firmware.ota
```

将 `--input` 和 `--key-file` 替换为实际的 OTA 包和对应密钥文件路径。`0.0.0.0` 是服务器监听地址；设备端需配置电脑的实际局域网地址，并使用相同的端口和路径。保持终端运行，切换固件包后需重启服务。两个后台均可在各自终端按 `Ctrl+C` 停止。

OTA 操作网页由时钟固件提供，无需在电脑上另行启动网页服务。设备连接 Wi-Fi 后，在同一局域网的浏览器打开串口日志中 `[WEB] Open http://...` 显示的地址（例如 `http://192.168.1.100/`），在页面的“固件升级”区域输入本地密钥 JSON 中的 `adminTokenHex` 并确认。首次使用前需按 [OTA 使用说明](docs/OTA_TESTS.md)配置并安装支持 OTA 的固件；网页触发还需要配置管理令牌。网页显示请求已保存后设备会重启，最终结果以设备恢复后的实际版本和状态为准。

Wi-Fi 和天气服务的本地凭据保存在 `app/clock_config_local.h`，OneNET 的本地凭据保存在 `app/onenet_config_local.h`；这些文件已被 Git 忽略，不要提交令牌或密钥。



## 根目录结构

### 目录

| 目录 | 作用 |
| --- | --- |
| `.git/` | Git 版本库数据，包括提交历史、引用和本地配置。 |
| `.settings/` | STM32Cube 开发环境的工程、工具链和软件包配置。 |
| `.vscode/` | VS Code 和 STM32Cube 相关的编辑器配置。 |
| `app/` | 应用代码，包括 Wi-Fi、天气、界面、闹钟、网页控制和 OneNET；字体、图片和页面资源分别位于 `font/`、`image/` 和 `page/`。 |
| `BSP/` | 板级外设驱动，如温湿度传感器、EEPROM、蜂鸣器、按键、RTC、ESP-AT 和屏幕；部分模块包含主机端测试。 |
| `build/` | CMake 生成的构建文件及固件产物，按 `Debug/`、`Release/` 等配置存放。 |
| `cmake/` | ARM 工具链配置及 STM32CubeMX 工程的 CMake 构建配置。 |
| `Core/` | STM32CubeMX 生成的主程序、外设初始化、中断处理和系统代码。 |
| `docs/` | 代码风格、硬件验收、闹钟和 OneNET 测试及连接说明等文档。 |
| `Drivers/` | STM32 HAL 驱动和 CMSIS 等厂商提供的代码。 |
| `EWARM/` | IAR Embedded Workbench 工程文件，以及对应的启动和链接配置。 |
| `Middlewares/` | FreeRTOS 内核、移植层和配置。 |
| `scripts/` | 固件构建、OneNET 配置和令牌处理等 PowerShell 脚本。 |
| `tools/` | 独立工具；`onenet-alarm-control/` 提供闹钟配置界面，`ota/` 提供 OTA 打包和固件服务。 |

`.git/` 是本地版本库数据；`build/` 是构建时生成的目录，已被 Git 忽略。`.vscode/` 也列在忽略规则中，但已被 Git 跟踪的文件仍会继续被跟踪。

### 文件

| 文件 | 作用 |
| --- | --- |
| `.clangd` | 指定 clangd 使用 `build/Debug` 中的编译数据库，辅助代码索引和诊断。 |
| `.gitignore` | 定义不纳入版本控制的构建产物、本地配置、凭据和临时文件等。 |
| `.mxproject` | STM32CubeMX 管理工程时使用的源文件和库等元数据。 |
| `AGENTS.md` | 仓库结构、代码风格、构建、测试及贡献约定。 |
| `Clock_CubeMX.ioc` | STM32CubeMX 工程配置，记录芯片、引脚及外设设置。 |
| `CMakeLists.txt` | 固件的主 CMake 构建文件，定义目标、源码和构建选项。 |
| `CMakePresets.json` | 定义 Ninja、ARM GCC 工具链及 Debug、Release 构建预设。 |
| `README.md` | 项目和根目录结构说明，即本文。 |
| `startup_stm32f407xx.s` | GCC 使用的启动汇编，包含复位入口和中断向量表。 |
| `STM32F407xx_FLASH.ld` | GCC 链接脚本，定义 Flash、RAM 和程序段布局。 |

## 构建和测试

在仓库根目录使用 PowerShell。固件构建需要 CMake 3.22+、Ninja 和 `arm-none-eabi-gcc`；脚本也会查找 STM32Cube 安装的 GCC 和 Ninja 工具包。

```powershell
# 构建 Debug 固件
.\scripts\build.ps1

# 清理并构建 Release 固件
.\scripts\build.ps1 -Configuration Release -Clean
```

固件的 `.elf`、`.hex` 和 `.bin` 产物位于 `build/Debug/` 或 `build/Release/`。主机端测试需要 MSVC C++ Build Tools：

```powershell
.\BSP\AHT20\tests\run_tests.ps1
.\BSP\Console\tests\run_tests.ps1
.\BSP\RTC\tests\run_tests.ps1
```

硬件验证步骤见 [docs](docs/)，代码约定见 [代码风格规范](docs/CODING_STYLE.md)。

