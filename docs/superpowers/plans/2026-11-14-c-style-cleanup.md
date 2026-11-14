# C 代码风格整理执行计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 按 `docs/CODING_STYLE.md` 整理 `app/`、`BSP/`、`Core/` 中的 C 源码和头文件。

**Architecture:** 按互不重叠的目录分工，先统一格式，再补充和翻译注释，最后进行差异检查、构建与回归验证。保留资源数据、字符串、外部接口和硬件行为。

**Tech Stack:** C11、STM32F407、FreeRTOS、CMake、ARM GCC、PowerShell。

**Spec:** `docs/CODING_STYLE.md`

## 约束

- 四空格缩进；控制语句左大括号同行；条件分支加大括号；每行一条语句。
- 注释使用中文，每个函数有功能、参数、返回说明，每个结构体有用途说明，成员有行尾中文注释。
- 保留 CubeMX 标记、版权许可声明、外部调用及 HAL/中断接口名称。
- 源码输入统一 UTF-8，旧资源保留 GBK 执行编码；保持数值数组、字符串执行字节及协议内容。
- 保留现有 `.settings/`、`.vscode/` 用户修改；不自动提交或烧录。

## 执行任务

- [x] 建立源码快照，检测本机构建工具，运行已有测试基线。
- [x] 整理 `app/alarm/`、`app/web/`、`app/onenet/`。
- [x] 整理 `BSP/`，包括已跟踪的测试与桩文件。
- [x] 整理 `Core/`，保留生成标记和初始化顺序。
- [x] 整理 `app/` 根目录、页面及资源文件中的注释与结构体初始化格式。
- [x] 核对函数/结构体注释覆盖、无括号分支及非中文说明；执行 Debug/Release 构建、主机回归和独立代码审查。

## 验证记录

- 初始工作区已有 `.settings/bundles-lock.store.json`、`.settings/bundles.store.json`、`.vscode/c_cpp_properties.json` 修改。
- 原 MSVC 主机测试入口因缺少 `vswhere.exe` 无法启动，将使用可用主机编译器运行相同测试源文件，并记录差异。
- Debug/Release 原始构建通过；Zig 主机编译器运行原 AHT20、Console（三种模式）、RTC 和 Core RTC 初始化测试均通过。
- 编码调整依据：独立预处理实验确认 UTF-8 中文头文件在 GBK 输入编译单元下报错；保留 GBK 执行编码并统一 UTF-8 输入，使用固件二进制比较验证兼容性。

## 最终结果

- 检查 123 个 C 源码和头文件；443 个函数、64 个结构体、348 个成员的注释覆盖审计通过。
- 四空格缩进、条件分支大括号、控制块布局、单行语句、结构体成员布局和无 Tab 检查通过。
- 123 个文件的数字、字符串及字符字面量序列与原始快照一致。
- Debug、Release，以及 RGB 测试与有源蜂鸣器组合配置均编译通过；默认 Debug 配置已恢复。
- 使用临时 Zig 主机编译器运行 AHT20、Console（三种模式）、RTC、Core RTC 初始化四组原有测试，均通过；原 MSVC 脚本仍依赖本机未安装的工具链。
- 两种配置的 42 个资源对象只读数据均与原始结果一致。固件尺寸不变，二进制差异经反汇编确认仅为断言源代码行号及编译时间。
- 独立审查通过；数组成员跨行及宏对齐 Tab 两处遗漏已修复并复核。最终构建与差异空白检查通过。
