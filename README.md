# Ku — 模型无关的智能系统通用编程语言

```text
模型负责能力，Ku 负责结构、契约、流程与执行
```

Ku 是一门模型无关的通用编程语言：统一描述普通程序、不同类型的模型、工具、
知识、任务和可恢复执行。云端模型、本地模型、视觉模型、语音模型、推理模型和未来模型
都应通过统一的 Ku 能力契约被组合、验证和替换。

Ku 的长期目标不是研发某一个模型，而是成为所有模型都可以使用的通用语言。
长期路线见 [`docs/KU_LONG_TERM_ROADMAP.md`](docs/KU_LONG_TERM_ROADMAP.md)。

当前开发版本处于 Alpha，已实现中文语法、自举编译器、字节码校验器、Register VM，
以及编译、保存、召回和执行代码记忆的链路。后续将推进 Schema、类型系统和统一模型接口。

规范语法是中文：

```ku
思 main(x, y) {
  返 x + y
}
```

## Ku v1

源语言语义基线见 [Ku v1 语义矩阵](docs/KU_V1_SEMANTICS.md)。

### 当前开发进展

2026-11-21 已验证的开发基线为提交 `4357073`，本轮变更汇总于 [PR #30](https://github.com/zhangshaoling/ku/pull/30)。
阅读时请结合所选分支与 [发布记录](https://github.com/zhangshaoling/ku/releases)，最新发布标签为 `v1.0.0`。

| 部分 | 当前能力 |
|---|---|
| 语言与编译 | 中文核心语法、默认自举前端、恢复编译器、双前端一致性门禁 |
| 执行与模块 | 严格字节码验证、Register VM、v2/ABI10、精确版本模块链接、C ABI |
| 开发工具 | `dao-ku --check`、最小 `--fmt`、汇编/反汇编、数值 AOT |
| 代码记忆 | 原生 ModuleStore、源码来源校验、父版本记录、单写入者锁、失败后重试恢复 |
| 智能体接入 | Python 绑定、MCP、可执行记忆示例和基础任务/工具循环 |

该基线的 Windows、Linux、macOS 内核 CI 和 Python 集成检查均通过。
本地功能 CTest 57/57、Python 集成测试 21/21 通过，验证命令见下文。
[CI 执行记录](https://github.com/zhangshaoling/ku/actions/runs/44879421520)

Record / Schema / Result、Typed IR、统一 Model Capability ABI、流式并发和可恢复任务仍在规划中。
完整 Thought 语义、记忆生命周期、原生长期运行网关和安装发行验证继续推进。

### 关键字

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

## 快速开始

以下命令在仓库根目录运行。先获取源码，再按平台构建。

```sh
git clone https://github.com/zhangshaoling/ku.git
cd ku
```

试用 PR #30 的开发内容时，合并前先切换到对应分支。

```sh
git switch codex/ku-memory-tooling-20261121
```

### Windows

首次构建需要安装仓库托管的工具链，下载源和校验信息见 [工具链说明](docs/TOOLCHAIN.md)。

```powershell
.\tools\bootstrap_toolchain.ps1
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
.\tools\build_kernel.ps1 -SkipTests
. .\tools\activate_toolchain.ps1
ctest --test-dir kernel/out/cmake --output-on-failure -E aot_performance_gate
```

### Linux / macOS

需要 C++20 编译器、CMake 3.25 及以上版本和 Ninja。Python 3.11 用于绑定及存储契约测试，
Cargo 可用时额外执行 Rust 绑定检查。

```sh
cmake -S . -B kernel/out/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build kernel/out/cmake --parallel 2
ctest --test-dir kernel/out/cmake --output-on-failure -E aot_performance_gate
```

### 编译、保存并执行

将下面的代码保存为 UTF-8 文件 `answer.ku`。

```ku
思 main() {
  返 42
}
```

Windows PowerShell 中运行以下命令。

```powershell
.\kernel\out\cmake\bin\dao-ku.exe --check answer.ku
.\kernel\out\cmake\bin\dao-ku.exe --fmt answer.ku
.\kernel\out\cmake\bin\dao-ku.exe answer.ku answer.dao
.\kernel\out\cmake\bin\dao-ku.exe --store .ku_agent_memory --identity ku:agent/answer 1.0.0 answer.ku
.\kernel\out\cmake\bin\dao-module.exe verify .ku_agent_memory ku:agent/answer 1.0.0
.\kernel\out\cmake\bin\dao-module.exe run .ku_agent_memory ku:agent/answer 1.0.0 main
```

最后一条命令输出 `42`。Linux/macOS 使用 `./kernel/out/cmake/bin/dao-ku` 和
`./kernel/out/cmake/bin/dao-module`，其余参数相同。

`--fmt` 将结果输出到 stdout。`dao-module run` 当前用于无参数的 `i64` 入口，
带参数函数通过 [C ABI](kernel/FFI.md) 或 Python 绑定调用。

原生代码记忆支持同一 identity/version 的内容与来源不可变、父记录校验及单写入者提交。
重叠写入返回忙碌错误，保存中断后可用相同输入重试。完整规则见 [模块与存储契约](kernel/MODULE_ABI.md)。

## 内核（实现）

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

## 可执行记忆闭环

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

## 验证

上面的 CTest 命令覆盖当前内核功能门禁，按现有 CI 配置排除独立的 AOT 性能阈值。
可用 `ctest --test-dir kernel/out/cmake --output-on-failure -R aot_performance_gate` 单独执行性能检查。

Windows 上安装 `pytest` 后，可运行 CI 使用的 Python 集成测试。

```powershell
python -m pip install pytest
$env:DAO_KERNEL_DLL_DIRS = Join-Path $PWD '.toolchain\llvm-mingw\bin'
python -m pytest -q tests/test_mcp_arithmetic.py tests/test_mcp_server_kernel.py tests/test_memory_kernel.py tests/test_task_kernel.py tests/test_agent_kernel.py tests/test_memory_loop.py
```

[全部 CI 记录](https://github.com/zhangshaoling/ku/actions)

## 权威文档

- [命名与权威](docs/KU_NAMING_AND_AUTHORITY.md)
- [Ku v1 语义](docs/KU_V1_SEMANTICS.md)
- [项目进度](docs/KU_PROJECT_PROGRESS.md)
- [文档导航](docs/README.md)
- [标准库接口](kernel/STDLIB_ABI.md)
- [模块与存储契约](kernel/MODULE_ABI.md)
- [二进制格式](kernel/FORMAT.md)

## 旧线

`dao/`（旧 C VM）与 `ku/`（更早的 Python 实现）保留为迁移输入，不再承载新功能。

MIT License
