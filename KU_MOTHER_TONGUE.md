# Dao / Ku 母语说明

本文是 Dao/Ku 的中文入口说明。旧版本把早期构想、语法草稿和乱码样例混在一起，容易让人误以为项目目标是“做一门普通中文脚本语言”。当前项目目标更窄，也更硬：

```text
thought = code = memory
```

Dao/Ku 要让“思”同时成为：

- 可执行的代码
- 可检查的数据结构
- 可持久化的记忆
- 可被 MCP 暴露给智能体调用的工具
- 未来可以自举、改写自身的材料

## 名字关系

- **Ku**：公开包名、仓库历史名、语言谱系名。
- **Dao**：当前主动推进的运行时线，包括中文优先语法、C VM、自举前端、可执行记忆和 MCP 网关。

以后写架构和运行时实现时，默认说 **Dao**；谈兼容层、包名、GitHub Linguist、历史语法时，可以说 **Ku**。

## 当前真实状态

Dao 已经不是单纯 Python 原型：

- C VM 可以运行已提交 bytecode，也可以通过 `--bootstrap` 运行源码。
- `dao/std/lexer.ku`、`parser.ku`、`compiler.ku` 是自举前端路径。
- MCP 默认通过 C VM 执行 `ku_eval`、`ku_call` 和经验记忆工具。
- 经验、缺口、数据集、数据记忆和任务队列可以持久化到 SQLite。
- Python 仍存在，但角色应收缩为测试、构建、夹具生成、MCP stdio 胶水和对拍基准。

一句话：Python 可以帮忙搭桥，但不能悄悄变回语义权威。

## 母语方向

Dao 的“中文”不只是关键字翻译。中文表层要服务于记忆和思维结构：

```ku
思 加一(x) {
  x + 1
}

加一(41)
```

这类源码最终应该能形成一条稳定路径：

```text
Dao source
  -> lexer/parser/compiler
  -> bytecode
  -> C VM
  -> memory database
  -> MCP callable tool
```

## 不再作为当前承诺的内容

旧稿里提到的一些想法仍可作为远期方向，但现在不要把它们写成已实现能力：

- 完整自然语言式语法
- 通用异步并发系统
- 自修改 AST 的完整产品化语义
- 自动强化/弱化记忆的完整机制
- 一次性脱离所有 Python 构建脚手架

这些方向可以回到路线图里，但当前模块完成标准以 `docs/MODULE_COMPLETION_PLAN.md` 为准。

## 最近优先级

1. 锁住 C VM、MCP、可执行记忆的默认路径。
2. 继续减少 Python 语义兜底。
3. 稳定标准库和模块 ABI。
4. 让记忆记录不只是数据库行，而能直接成为可调用 thought/tool surface。
5. 在 Dao 足够强以后，再把更多 bootstrap 生成逻辑移出 Python。

## 验证入口

```powershell
.\tools\test.ps1 -q
.\tools\verify_module.ps1 all
```

项目是否前进，不看口号，看这些门是否继续通过。
