# 编译诊断

v1.2.0 开发中的 `dao-ku` 在编译失败时输出文件、行列、错误原因、源码行和 `^` 指示符。
已发布版本以 [Releases](https://github.com/zhangshaoling/ku/releases) 为准。

例如，`main.ku` 包含：

```ku
思 main() {
  返 缺失
}
```

默认自举前端的错误输出形如：

```text
main.ku:2:5: error: compile failed: undefined identifier at offset 19
2 |   返 缺失
  |      ^
```

可以通过普通编译、`--check`、`--fmt`、`--test` 和 `--store` 获得相同的诊断格式。
`--recovery` 使用恢复前端，其前缀为 `recovery compile failed:`；两套前端保留各自的错误原因。
诊断写入 stderr，编译失败时不输出格式化源码或生成模块。

## 位置规则

- 行号、列号从 1 开始。列号按 Unicode 码点计算，不按 UTF-8 字节数计算；一个 Tab 在列号中计为一个字符。
- LF 和 CRLF 均支持。源码行中的 Tab 按四列制表位展开，常见中文宽字符按两个显示单元对齐。
- 文件末尾错误可指向最后一行之后的空行。
- 源码导入保留原文件位置，包括嵌套导入、命名空间重写和自动插入的记忆 Host 声明。
- 导入无法读取或形成循环时，位置指向发起该次导入的声明。
- 旧的 `at offset N` 文本保留兼容，其偏移来自所用前端的输入，可能属于组合源码；定位原文件时应使用文件与行列。

`^` 针对常见等宽终端对齐；字体、复杂 emoji 和组合字形的实际显示宽度可能不同。
无可靠源码位置的错误（例如非法 UTF-8、编译器 VM 或 Host 内部错误）保留原始文本，不生成猜测的行列。

## C++ 嵌入

`dao::km::Options` 与 `dao::selfhost::Options` 提供可选 `source_name` 和
`CompilerDiagnostic* diagnostic`。失败时可读取原文件名称、源码和字节偏移；
没有可靠位置时 `offset == std::string::npos`。源码导入名称为 resolver 接收的逻辑路径。
每次编译会清空传入的诊断对象。该元数据不改变冻结的 `dao_error` C ABI。

源码组合的 `source_append` Host 负责复制字节并保留来源片段，`source_context` 记录当前解析输入；
词法、语法、名称解析与 lowering 仍由 `.ku` 前端完成。
