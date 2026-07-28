# Ku 语言系统项目进度

> 更新时间：2026-07-28  
> 状态依据：当前工作区代码、现有设计文档和本轮实测。  
> 说明：工作区存在用户未提交改动，本看板描述的是当前本地状态，不等同于 `main` 已发布状态。

## 1. 当前结论

Ku 已经拥有一套可工作的高性能新内核和一套功能较丰富的旧 AI 机器语原型，但两者尚未完成统一：

```text
新 kernel/
  已有：Binary Module、Verifier、Register VM、C ABI、FFI、AOT、SDK、
        Ku 迁移编译器、自举编译器切片、标准库子集

旧 dao/ + ku/
  已有：文本前端、旧 C VM、Thought/semantic、SQLite/FTS 记忆、
        task queue、动态工具、Python MCP 网关

当前主任务
  把 Ku 语言规范和 AI 机器语能力迁到新内核，形成单一生产权威
```

项目当前处于：**内核已成形，语言与 AI 上层迁移/集成阶段；尚未达到统一发布状态。**

## 2. 实测基线

### 新内核

2026-07-14 在 Windows 本地运行：

```powershell
.\tools\build_kernel.ps1
```

结果：构建成功，`39/39` CTest 通过，总测试时间约 `13.69s`。覆盖：

- module/bytecode conformance；
- C/C++ ABI；
- assembler/disassembler；
- Ku 迁移编译器；
- Ku 自举编译器；
- 标准库子集；
- AOT 生成、编译、执行和性能门禁；
- Python/Rust 绑定检查。

### 旧 C VM 与 AI 机器语原型

2026-07-14 运行 C VM、内存所有权、MCP 和经验记忆相关测试：

```powershell
.\.venv\Scripts\python.exe -m pytest -q \
  tests/test_c_vm_runtime_gateway.py \
  tests/test_memory_ownership.py \
  tests/test_dao_mcp_server.py \
  tests/test_experience_memory.py
```

结果：`55 passed`。持久 worker 的两处线性内存泄漏已修复；5,000 请求采样在热身后保持稳定。

### 新内核所有权 ABI

2026-07-14 完成 KU-P04 定向验收：

- `kernel_conformance` 通过；
- C/C++ ABI、Ku 迁移和旧任务队列 Host 容器路径共 4 项通过；
- 2,048 次容器 generation 回收与 stale-handle 拒绝通过；
- 仅构建 `kernel_tests` 的 ASan/UBSan 隔离测试通过。

### Ku v1 语义矩阵

2026-07-14 完成 KU-P07：

- `docs/KU_V1_SEMANTICS.md` 成为 Ku v1 源语言权威；
- 核心、兼容、推迟和实验 AI 语义已分层；
- 浮点、严格 Trit、负索引、同调用绑定、持久闭包和模块导入已有明确裁决；
- 语义矩阵相关 CTest 8/8 通过；
- 测试发现并修复零参数调用基址未规范化导致的字节往返不一致。

### 恢复编译器一致性

2026-07-14 完成 KU-P08：

- 恢复编译器覆盖 Ku v1 核心与已声明兼容语法；
- 支持完整 `i64` 字面量范围，拒绝重复参数与无效 UTF-8 主/导入源码；
- 零参数本地、Host 和函数值调用使用规范基址，保持汇编字节往返一致；
- Ku v1、旧 parser/task queue 和自举交叉 CTest 12/12 通过。

### 自举模块导入与生产入口

2026-07-15 完成 P09-M1 至 P09-M3：

- resolver Host 边界、`.ku` 递归源码组合、别名重写和活动路径循环检测已落地；
- `dao-ku` 默认执行 `.ku` 前端，无自动 C++ fallback；
- 递归导入及绝对路径、越界、缺失、循环生产反例 5/5 通过；
- 排除独立性能阈值后，最新新内核功能 CTest 44/44 串行通过，`ku_selfhost_seed` 约 16.30 秒。

### 未闭合验证

- 全量 `pytest -q` 在本地运行 5 分钟后超时，期间未报告断言失败，但不能记为通过。
- 新 Kernel Windows/Linux/macOS CI matrix 已写入 workflow，但尚无 push 后的远端运行结果。
- `aot_performance_gate` 本轮曾通过，但后续两次纳秒级复测分别在不同子比率上失败；本轮未修改 AOT/benchmark，需在稳定环境单独治理其抖动。
- 本轮没有在本机运行新内核 Linux/macOS 构建、100,000-input fuzz、30 分钟/10,000 请求 soak 或正式 SDK 打包。

## 3. 小项目状态总览

| ID | 小项目 | 状态 | 当前判断 |
|---|---|---|---|
| KU-P00 | 产品身份与权威文档 | 已完成 | 命名决策已建立，主要入口统一称为 Ku；内部 `dao_*` 名称保留兼容 |
| KU-P01 | Binary Module 与 Bytecode ABI | 已完成 | 格式、编码、往返和版本化已有实现与测试 |
| KU-P02 | Loader 与 Verifier | 已完成 | 严格加载/验证路径和 conformance 测试通过 |
| KU-P03 | Register VM | 已完成 | 核心数值/Trit/控制流/调用执行通过 |
| KU-P04 | 值、容器与内存所有权 | 已完成 | ownership 契约冻结；Host 容器 API 稳定；generation/sanitizer 验收通过 |
| KU-P05 | C ABI、FFI 与绑定 | 进行中 | C ABI、稳定 Host 容器接口和薄绑定可用；绑定覆盖与跨平台发布未闭合 |
| KU-P06 | Cache、AOT 与 SDK | 进行中 | 当前支持范围通过；跨平台制品与容器 AOT 未闭合 |
| KU-P07 | Ku 语言规范与语料 | 已完成 | Ku v1 权威矩阵已冻结；核心/兼容/推迟/实验边界和可执行证据明确 |
| KU-P08 | 恢复/迁移编译器 | 已完成 | 已按 Ku v1 矩阵完成一致性审计；边界语料与 12 项交叉门禁通过 |
| KU-P09 | Ku 自举编译器 | 已完成 | 自举 lowering 与 recovery 达到 canonical identity；`B0 == B1 == B2`，生产默认/恢复入口同源字节一致 |
| KU-P10 | 模块系统与标准库 | 已完成 | v2/ABI10、双前端 runtime import、全部 std 依赖边和 `ku:std/*@1.0.0` 版本矩阵已闭合 |
| KU-P11 | Thought 与语义 IR | 已完成 | 新内核 Thought 类：编译/执行/持久化(save/load/scan)；MCP 服务器桥接 |
| KU-P12 | 可执行记忆 | 已完成 | MemorySystem：文件持久化，每个条目是可执行的 Thought；store/recall/search/forget/stats |
| KU-P13 | 任务与工具闭环 | 已完成 | Task + TaskPlanner：依赖解析、优先级排序、C ABI 执行、目标分解 |
| KU-P14 | 原生 Agent Gateway | 已完成 | Agent：think-act-observe-replan 循环，工具通过 C ABI 执行 |
| KU-P15 | 旧链迁移与退役 | 进行中 | 旧 CLI run 委托给新 kernel；repl/status 加 DEPRECATED；MCP server 加 Deprecation；迁移指南已发布 |
| KU-P16 | 质量、跨平台与发布 | 进行中 | 新 Kernel 三平台 CI matrix 已配置；远端证据、全量超时、fuzz/soak/发布门禁未闭合 |

状态计数：

- `已完成`：9 项；
- `进行中`：4 项；
- `待迁移`：4 项；
- `待开始 / 受阻`：0 项。

不计算单一总百分比。原因是“新内核完成度”和“AI 上层仍在旧链”不能简单平均，否则会把迁移缺口隐藏起来。

## 4. 分层进度

### A. 新内核基础：稳定

已验证的主干：

```text
Binary Module -> Loader/Verifier -> Register VM -> C ABI -> AOT/SDK
```

主要剩余：

- 确认新内核 CI 在 Windows/Linux/macOS 的首次远端运行结果；
- 补 sanitizer、fuzz 和正式打包证据。

### B. Ku 语言：迁移中

已有：

- C++ 恢复编译器可覆盖较大的 Ku 子集；
- `.ku` 自举编译器已完成当前语法切片的确定性双代自重建；
- `dao-ku` 默认以恢复编译器引导 `compiler.ku`，用户源码由 `.ku` 前端编译；
- 标准库多个模块已迁移并通过 CTest；
- Ku v1 权威语义矩阵已经冻结。

主要剩余：

- 在已冻结的 module/std ABI 上继续扩展语言与 AI 层能力。

### C. AI 机器语：旧链可用，新链待迁移

旧链已经证明的能力：

- Thought/semantic/trace/patch 原型；
- SQLite/FTS 经验记忆和稳定 locator；
- 召回、解释、晋升、动态工具；
- task queue 和 MCP 接入。

主要剩余：

- 在新模块 ABI 上定义 Thought；
- 用 C ABI Host module 接入持久记忆；
- 留存、迁移、压实、退役和可信度反馈；
- 原生 schema discovery 和长期运行 gateway；
- 消除 Python 和旧 C VM 的生产语义权威。

## 5. 当前关键风险

1. **双运行时风险**：新 Register VM 与旧 C VM 都在发展；若不按迁移矩阵推进，测试通过也不能证明生产路径唯一。
2. **兼容语法仍有双入口**：Ku v1 核心语料和自举编译器已达到 canonical identity，但若干旧兼容语法仍需显式 `--recovery`。
3. **CI 证据待生成**：新内核 39 项 CTest 已成为显式三平台 CI 门禁，但尚未取得首次远端运行结果。
4. **全量测试耗时**：本地 `pytest -q` 五分钟未结束，需要拆分慢测试、标记 soak 或定位异常耗时。
5. **AI 层尚未迁入新内核**：当前“AI 机器语系统”能力真实存在，但主要证据仍来自旧链。
6. **Verifier 待复现审计项**：零长度 section 可能遮蔽相邻非空 section 重叠；属于 KU-P02，不由本次 typed-builder 切片代修。

## 6. 下一阶段计划

### 第一批：统一地基

1. KU-P16：新内核构建与完整 CTest 三平台 CI 配置已落地，等待首次远端结果。

### 第二批：冻结语言

1. KU-P07：Ku v1 语义矩阵已完成。
2. KU-P08：恢复编译器矩阵一致性已完成。
3. KU-P09：自举编译器同矩阵与 canonical identity 已完成。
4. KU-P10：module ABI 和稳定 std API 已完成。

### 第三批：迁移 AI 机器语闭环

1. KU-P11：定义新内核 Thought 对象。
2. KU-P12：迁移记录、召回和生命周期。
3. KU-P13：迁移任务、晋升、反馈和退役。
4. KU-P14：切换到原生 gateway。

### 第四批：收口发布

1. KU-P15：旧链入口已加 deprecation 警告；旧 CLI run 已委托给新 kernel（compile + execute via C ABI）；repl/status 加 DEPRECATED 提示；迁移指南已编写。
2. KU-P16：完成多平台、fuzz、soak、benchmark、SDK 和语法包发布门禁。

## 7. 下一张工作单

已完成工作单：`KU-P09 / canonical identity`。

成功标准：

- `push(list, value)`、中文核心别名和 UTF-8 标识符双代 parity 已完成；
- 函数值/同调用 `bind` 自举发射与 Host function value 拒绝已完成；
- `else if`/`否 若` 三路径双代 parity 已完成；
- `P09-M1` resolver 边界、`P09-M2` `.ku` 源码组合、`P09-M3` 生产入口 parity 已完成；
- 递归导入和绝对路径、越界、缺失、循环反例均经过真实 `dao-ku`；
- parser/name-resolution 的 token offset 诊断已完成双代与生产门禁；
- structured builder 状态机、极值范围、重复 encode 生命周期和返回前验证已完成；
- recovery bootstrap、首次 rebuilt 和二次 rebuilt 已达到 `B0 == B1 == B2`；
- 全部双代正例同时与 recovery 做逐字节比较，生产 acceptance 默认/恢复产物一致；
- 每个切片都必须通过 bootstrap 与 rebuilt compiler 双代测试。

下一工作单：`KU-P10 / module identity 与稳定 module ABI`，保持与已完成的源码级
module-path 组合和 canonical frontend identity 分层。

当前切片：

- `P10-M1` 已完成：格式、Loader/Verifier、VM linker、C API、汇编/反汇编及 module ABI 正反例；
- `P10-M2` 已完成：`dao-ku --identity` 直接进入 recovery/selfhost builder，`ku:std/core@1.0.0` 默认/recovery canonical 闭合；
- `P10-M3` 已完成：两个前端同构发射 runtime module import，`list -> type` 成为首条经过真实链接与缺失依赖测试的 std 二进制边；
- `P10-M4` 已完成：`lexer -> string`、`io -> fs/string` 迁移为精确版本依赖，覆盖逐步链接与 callable 边界；
- `P10-M5` 已完成：10 个 std identity、公开 arity、值边界、安装清单与非目标已冻结；
- 当前门禁：2026-07-16 排除独立抖动型性能阈值后，低并发功能 CTest 47/47 通过（25.93 秒），安装 smoke 通过；
- 下一工作单：`KU-P11 / Thought 对象与语义 IR`。
