# 代码风格规范

新增或修改本仓库的手写代码时，应遵循以下约定。

## C 语言

- 使用四个空格缩进，不使用 Tab 缩进。
- `if`、`else if` 和 `else` 分支必须使用 `{}`，即使分支只有一条语句。
- 每行只写一条语句。赋值、函数调用、`return` 等语句分别换行，不将分支体压缩到条件所在行。
- 控制语句的左大括号 `{` 与条件或语句头同行，前面保留一个空格；右大括号 `}` 换行并与语句头对齐。`else`、`else if` 与前一个 `}` 同行。
- 函数和变量使用 `snake_case`，宏使用 `UPPER_SNAKE_CASE`，类型别名以 `_t` 结尾。

推荐写法：

```c
if (ready) {
    update_clock();
    refresh_display();
} else {
    return;
}
```

不要使用以下写法：

```c
if (ready)
{
    update_clock(); refresh_display();
}

if (!ready) return;
```

`for`、`while`、`switch` 等控制语句的代码块同样采用左大括号同行的格式。

### 注释规范

- C 语言注释一律使用中文，包括文件注释、函数注释、块注释和行内注释。
- 每个函数都必须在定义前添加函数级注释，包括 `static` 内部函数、回调函数和中断处理函数。
- 函数级注释应说明功能、参数和返回值；没有参数或返回值时注明“无”。

示例：

```c
/**
 * 功能：判断闹钟编号是否位于有效范围。
 * 参数：alarm_index，待检查的闹钟编号，从零开始。
 * 返回：编号有效时返回 true，否则返回 false。
 */
static bool alarm_index_valid(uint8_t alarm_index) {
    return alarm_index < ALARM_COUNT;
}
```

### 结构体与成员注释

- 每个结构体定义前必须添加中文注释，说明结构体的用途。
- 每个成员变量独占一行，并在声明末尾使用 `//` 中文行注释阐明其作用；必要时补充单位、取值范围或特殊值含义。

示例：

```c
/** 保存日历日期。 */
typedef struct {
    uint16_t year;   // 年份
    uint8_t month;   // 月份，取值为 1～12
} clock_date_t;
```

## JavaScript

`tools/onenet-alarm-control/` 中的模块使用两空格缩进、camelCase 命名、单引号和分号，保持 ES 模块的导入、导出方式。

## 生成代码与文件编码

- 修改 CubeMX 生成代码时，将自定义逻辑放在 `USER CODE` 区块内。
- 新增应用文件使用 UTF-8。
- 保留 `CMakeLists.txt` 中 `CLOCK_LEGACY_SOURCES` 的 GBK 编译设置，避免破坏旧字体及页面资源。

仓库尚未配置统一的格式化或代码风格检查工具，提交前应人工检查上述约定。
