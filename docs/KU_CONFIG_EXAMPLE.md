# 用 Ku 编写配置校验工具

`ku-config` 是 v1.2.0 开发中的原生示例命令。解析、必填项、重复键、数值和范围检查
由 [config_validator.ku](../demos/config_validator.ku) 实现；C++ 宿主负责读文件、调用 VM 和输出结果。
运行工具不需要 Python，也不需要临时编译源码。

## 使用

从源码构建后，在仓库根目录执行：

```powershell
. .\tools\activate_toolchain.ps1
cmake --build kernel/out/cmake --parallel 2
.\kernel\out\cmake\bin\ku-config.exe demos/service.conf
```

SDK 解压后，在 SDK 根目录执行：

```powershell
.\bin\ku-config.exe share/dao/examples/service.conf
.\bin\dao-ku.exe --test share/dao/examples/config_validator_tests.ku
.\bin\dao-ku.exe --test --recovery share/dao/examples/config_validator_tests.ku
```

Linux/macOS 使用对应的 `./bin/ku-config`、`./bin/dao-ku`，不加 `.exe`。
成功输出 `valid <文件>`。`ku-config --help` 显示简短用法。

## 配置格式

UTF-8 文本，每行一个 `key=value`：

```text
# 服务配置
name=示例服务
port=8080
workers=4
```

- `name` 必填，去除首尾 ASCII 空格、Tab 和 CR 后不能为空；可包含中文和等号。
- `port` 必填，为 1–65535 的 ASCII 十进制整数。
- `workers` 可省略；提供时为 1–256 的 ASCII 十进制整数。
- 数值允许前导零；不接受正负号、小数、全角数字和尾随字符。逐位检查上限，避免整数溢出。
- 支持 LF、CRLF、空行和整行 `#` 注释。首个等号分隔键和值。
- 键区分大小写，未知键和重复键均报错；不支持引号转义、行尾注释、嵌套节或变量展开。

工具收集全部配置错误并写入 stderr，例如：

```text
service.conf:2: error: port must be a decimal integer in 1..65535 [port]
service.conf:4: error: duplicate key [port]
service.conf: error: missing required key [name]
```

缺失字段没有对应源码行，因此不生成行号。退出码为 `0`（有效）、`1`（配置无效）、
`2`（用法、文件读取、UTF-8、模块加载或执行错误）。失败时 stdout 为空，工具不修改配置文件。

## 实现和测试边界

构建时由默认自举前端将 `.ku` 编译为随 SDK 安装的 `config_validator.dao`。
运行时使用 Register VM 和 C ABI；文件内容在调用期间由宿主持有，返回的错误列表在 VM 销毁前输出。

校验器内部使用固定只读的 `校验错误` Record（行号、字段、代码、消息），在 `校验` 返回前
通过 `导出错误` 逐项创建普通 Map。公开结果保持 `{ok, errors}`，错误项保持
`{line, key, code, message}`；修改导出 Map 不改变内部 Record。这不是通用 Schema 转换。
随包模块现为 v3/ABI11，`ku-config` 在自己的 VM 上显式开启 Record 能力后加载；
通用 VM 和 Store 默认策略仍不变。工具仍只校验，不返回解析后的配置或隐式补 workers 缺省。

三个显式、只读的 Host 能力 `source_len/1`、`source_byte/2`、`source_slice/3`
与自举编译器共用实现。长度与下标按 UTF-8 字节计算，切片必须位于合法 UTF-8 边界。
它们也在原生测试 VM 中注册，提供文本处理，不提供文件或网络访问。

12 个原生 Ku 测试覆盖规则、边界、行号、Record 构造和独立 Map 导出；CLI 测试使用真实配置文件验证完整输出和退出码；
独立门禁比较两套前端生成的模块字节。SDK 验证在无开发工具链搜索路径的解压目录中运行示例。
