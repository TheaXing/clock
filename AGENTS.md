# Repository Guidelines（仓库贡献指南）

## 项目结构与模块划分

本项目基于 STM32F407、C11 和 FreeRTOS，实现天气时钟及闹钟控制。

- `app/`：应用服务、闹钟、网页控制和 OneNET 接入；字体、图片及页面资源位于 `app/font/`、`app/image/` 和 `app/page/`。
- `BSP/`：外设驱动及各模块的 `tests/` 测试目录。
- `Core/`：CubeMX 生成的初始化和中断处理代码；外设配置保存在 `Clock_CubeMX.ioc`。
- `Drivers/`、`Middlewares/`：厂商 HAL、CMSIS 和 FreeRTOS 源码。
- `scripts/`、`cmake/`：构建与配置工具；`tools/onenet-alarm-control/`：独立的浏览器控制工具；`docs/`：硬件验收说明。

## 构建与本地开发

在仓库根目录使用 PowerShell 执行命令。固件构建需要 CMake 3.22+、Ninja 和 ARM GCC（`arm-none-eabi-gcc`）；构建脚本会查找 STM32Cube 安装的 GCC/Ninja 工具包。

- `.\scripts\build.ps1`：配置并构建 Debug 固件。
- `.\scripts\build.ps1 -Configuration Release -Clean`：清理后重新构建 Release 固件。
- `node tools/onenet-alarm-control/server.mjs`：启动控制工具，访问 `http://127.0.0.1:8787/`；需要 Node.js 20+，无需安装 npm 依赖。

固件产物为 `Clock_CubeMX.elf`、`.hex` 和 `.bin`，输出到 `build/Debug/` 或 `build/Release/`。

## 代码风格与命名规范

新增和修改代码须遵循 [代码风格规范](docs/CODING_STYLE.md)。C 语言要求四空格缩进、`if` 分支必须使用大括号、左大括号同行、每行一条语句。

C 语言注释一律使用中文。每个函数必须添加函数级注释，每个结构体必须添加用途说明，每个结构体成员变量必须使用行尾 `//` 注释阐明作用。命名、JavaScript 风格、生成代码和文件编码约定详见该文档。

## 测试要求

主机端 C 测试使用断言、自定义检查和 HAL/FreeRTOS 桩，通过 MSVC C++ Build Tools 编译，并启用 `/W4 /WX`：

```powershell
.\BSP\AHT20\tests\run_tests.ps1
.\BSP\Console\tests\run_tests.ps1
.\BSP\RTC\tests\run_tests.ps1
```

测试文件命名为 `test_<module>.c`，放入 `BSP/<module>/tests/`。根目录 `tests/` 被 Git 忽略，应以已纳入版本控制的 BSP 测试为可复现基线。当前未设覆盖率门槛；行为变更应补充回归用例，并按 `docs/` 中的相关清单完成硬件验证。

## 提交与 Pull Request 规范

历史提交同时使用 `feat:` 前缀和普通中文摘要。建议标题简短并说明具体动作，例如 `feat: 添加闹钟功能`。PR 应说明行为变化、关联相关 Issue，并列出构建、测试和硬件验证结果；涉及屏幕或网页界面的修改应附截图。

## 安全与本地配置

Wi-Fi 和天气服务凭据放入被 Git 忽略的 `app/clock_config_local.h`，OneNET 凭据放入 `app/onenet_config_local.h`。不要提交令牌或密钥。
