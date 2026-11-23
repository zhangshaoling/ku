<div align="center">

# Ku

**模型无关的智能系统通用编程语言**

中文语法 · 自举编译器 · 可验证模块 · 可执行记忆

[![CI](https://github.com/zhangshaoling/ku/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/zhangshaoling/ku/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/zhangshaoling/ku)](https://github.com/zhangshaoling/ku/releases/latest)
[![License](https://img.shields.io/github/license/zhangshaoling/ku)](LICENSE)

[下载](https://github.com/zhangshaoling/ku/releases/latest) · [快速开始](#快速开始) · [文档](docs/README.md) · [版本变化](docs/releases/v1.2.0.md) · [路线图](docs/KU_LONG_TERM_ROADMAP.md)

</div>

Ku 使用中文描述程序，通过自举编译器生成可验证的二进制模块，并在 Register VM 中执行。
代码可以带着版本和来源存入记忆，随后召回、验证和再次调用。
长期目标是以统一契约连接普通程序、模型能力、工具、知识与可恢复任务。

```ku
思 main(x, y) {
  返 x + y
}
```

## v1.2.0

Alpha · 2026-11-23。本版本先发布 Windows x64 SDK，加入原生测试、源码诊断、
同模块标量 Record 全链路和配置校验工具。Linux/macOS 附件待对应验证后提供。[查看版本说明](docs/releases/v1.2.0.md)

| 部分 | 当前能力 |
|---|---|
| 语言与编译 | 中文核心语法、默认自举前端、恢复编译器、双前端一致性门禁 |
| 执行与模块 | 严格字节码验证、Register VM、v1/v2 兼容与显式 v3/ABI11 Record、精确版本链接、C ABI |
| 开发工具 | `dao-ku --test/--check/--fmt`、`ku-config`、汇编/反汇编、数值 AOT |
| 代码记忆 | 原生 ModuleStore、源码来源校验、父版本记录、单写入者锁、失败后重试恢复 |
| 智能体接入 | Python 绑定、MCP、可执行记忆示例和基础任务/工具循环 |

CI 在 Windows、Linux、macOS 上执行内核功能测试和解压后的 SDK 验证，Windows 另执行 Python 集成测试。
[查看运行结果](https://github.com/zhangshaoling/ku/actions)

同模块标量 Record 已接通双前端、VM、存储和只读绑定；Schema / Result、嵌套 Record、Typed IR、
统一 Model Capability ABI、流式并发和可恢复任务继续开发。
完整 Thought 语义、记忆生命周期、原生长期运行网关和安装发行验证继续推进。

<details>
<summary>中文语法与示例</summary>

源语言契约见 [Ku v1 语义矩阵](docs/KU_V1_SEMANTICS.md)。

| 中文 | 含义 | 中文 | 含义 |
|---|---|---|---|
| `思` | 定义函数 | `返` | 返回 |
| `若` / `否` | 分支 | `当` | 循环 |
| `遍 … 于` | 遍历 | `断` / `续` | break / continue |
| `试` / `抛` / `捕` | 异常 | `引 … 别` | 导入模块 |
| `真` / `假` | Trit（+1 / -1） | `空` | null |

英文关键字（`thought`、`if`、`return`、`import` …）作为兼容语法保留，中文关键字用于规范示例。

### 例子

```ku
思 分类(x) {
  若 x > 10 { 返 3 }
  否 若 x > 0 { 返 2 }
  否 { 返 1 }
}

思 求和(items) {
  total = 0
  遍 item 于 items { total = total + item }
  返 total
}
```

</details>

## 快速开始

从 [最新 Release](https://github.com/zhangshaoling/ku/releases/latest) 下载适合系统和架构的 SDK ZIP，
解压后使用 `bin` 中的 `dao-ku` 与 `dao-module`。Windows 包为 x64，Darwin 表示 macOS，
其他架构以附件名称为准。每个 ZIP 附有 SHA-256 校验文件。

从源码构建时，在仓库根目录按平台执行下面的命令。

```sh
git clone https://github.com/zhangshaoling/ku.git
cd ku
```

<details>
<summary>Windows 从源码构建</summary>

首次构建需要安装仓库托管的工具链，下载源和校验信息见 [工具链说明](docs/TOOLCHAIN.md)。

```powershell
.\tools\bootstrap_toolchain.ps1
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
.\tools\build_kernel.ps1 -SkipTests
. .\tools\activate_toolchain.ps1
ctest --test-dir kernel/out/cmake --output-on-failure -E aot_performance_gate
```

</details>

<details>
<summary>Linux / macOS 从源码构建</summary>

需要 C++20 编译器、CMake 3.25 及以上版本和 Ninja。Python 3.11 用于绑定及存储契约测试，
Cargo 可用时额外执行 Rust 绑定检查。

```sh
cmake -S . -B kernel/out/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build kernel/out/cmake --parallel 2
ctest --test-dir kernel/out/cmake --output-on-failure -E aot_performance_gate
```

</details>

### 编译、保存并执行

将下面的代码保存为 UTF-8 文件 `answer.ku`。

```ku
思 main() {
  返 42
}
```

在解压后的 SDK 目录用 PowerShell 运行以下命令。

```powershell
.\bin\dao-ku.exe --check answer.ku
.\bin\dao-ku.exe --fmt answer.ku
.\bin\dao-ku.exe answer.ku answer.dao
.\bin\dao-ku.exe --store .ku_agent_memory --identity ku:agent/answer 1.0.0 answer.ku
.\bin\dao-module.exe verify .ku_agent_memory ku:agent/answer 1.0.0
.\bin\dao-module.exe run .ku_agent_memory ku:agent/answer 1.0.0 main
```

最后一条命令输出 `42`。Linux/macOS 使用 `./bin/dao-ku` 和 `./bin/dao-module`，其余参数相同。
源码构建的工具位于 `kernel/out/cmake/bin`。

`--fmt` 将结果输出到 stdout。`dao-module run` 当前用于无参数的 `i64` 入口，
带参数函数通过 [C ABI](kernel/FFI.md) 或 Python 绑定调用。

原生代码记忆支持同一 identity/version 的内容与来源不可变、父记录校验及单写入者提交。
重叠写入返回忙碌错误，保存中断后可用相同输入重试。完整规则见 [模块与存储契约](kernel/MODULE_ABI.md)。

## 内核（实现）

<details>
<summary>执行链路、ABI 与绑定</summary>

`kernel/` 是新内核的实现权威：

```text
.ku 源码 → 编译器 → .dao 二进制模块 → 校验器 → Register VM → C ABI → 结果 / 记忆 / 工具
```

- 确定性二进制模块（v2 / ABI10；v1 / ABI9 兼容）
- 严格 section / 指令校验器
- `i64` 算术（检查溢出）、Trit 三值逻辑
- C ABI / FFI / AOT / Python·Rust 绑定
- 当前迁移阶段支持可执行记忆：每个记忆可以是一个可调用的 Thought
- 长期方向是 Model Capability、Schema、任务、流式结果和可恢复执行

详见 [`kernel/README.md`](kernel/README.md)。

</details>

## 可执行记忆闭环

<details>
<summary>运行 Python 与 MCP 示例</summary>

Python 示例展示编译、文件记忆、重新实例化召回，以及将记忆注册成 Agent 工具的过程。
完成 Windows 构建后，用 Python 3.11 运行。

```powershell
$env:DAO_KERNEL_DLL_DIRS = Join-Path $PWD '.toolchain\llvm-mingw\bin'
python demos/memory_loop.py
```

```text
写 .ku → 编译 .dao → 持久化 → 召回执行 → 注册工具 → Agent think-act-observe
```

MCP 服务暴露 `ku_memory_store` / `ku_memory_recall` / `ku_memory_search` / `ku_memory_stats`，
让记忆成为可被智能体直接调用的工具。接入方法见 [Agent/MCP 指南](docs/AGENT_MCP_ONBOARDING.md)。
当前 Agent 主要按名称匹配工具，任务分解仍有占位逻辑。

</details>

## 验证

<details>
<summary>功能测试与性能检查</summary>

上面的 CTest 命令覆盖当前内核功能门禁，按现有 CI 配置排除独立的 AOT 性能阈值。
可用 `ctest --test-dir kernel/out/cmake --output-on-failure -R aot_performance_gate` 单独执行性能检查。

Windows 上安装 `pytest` 后，可运行 CI 使用的 Python 集成测试。

```powershell
python -m pip install pytest
$env:DAO_KERNEL_DLL_DIRS = Join-Path $PWD '.toolchain\llvm-mingw\bin'
python -m pytest -q tests/test_mcp_arithmetic.py tests/test_mcp_server_kernel.py tests/test_memory_kernel.py tests/test_task_kernel.py tests/test_agent_kernel.py tests/test_memory_loop.py
```

[全部 CI 记录](https://github.com/zhangshaoling/ku/actions)

</details>

## 权威文档

- [命名与权威](docs/KU_NAMING_AND_AUTHORITY.md)
- [Ku v1 语义](docs/KU_V1_SEMANTICS.md)
- [项目进度](docs/KU_PROJECT_PROGRESS.md)
- [文档导航](docs/README.md)
- [标准库接口](kernel/STDLIB_ABI.md)
- [模块与存储契约](kernel/MODULE_ABI.md)
- [二进制格式](kernel/FORMAT.md)
- [版本发布流程](docs/RELEASING.md)

<details>
<summary>历史与迁移资料</summary>

`dao/`（旧 C VM）与 `ku/`（更早的 Python 实现）保留为迁移输入，不再承载新功能。
早期进度看板和记忆架构设计收录于 [历史归档](docs/archive/README.md)。

</details>

[MIT License](LICENSE)
