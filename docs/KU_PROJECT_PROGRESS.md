# Ku 语言系统项目进度

> 更新时间：2026-11-23
> 状态依据：当前工作区代码、现有设计文档和本轮实测。  
> 说明：本看板保留开发过程中的分阶段证据，正式版本与附件以 GitHub Releases 为准。

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

项目当前处于：**内核与代码记忆具备发布基础，完整语言与 AI 上层继续迁移和集成。**

## 2. 实测基线

### keys(Map) 双前端兼容修复（2026-11-23）

- 自举前端使用完整四字节 `keys` 识别，修复长度误写为 5 导致内建调用无法识别、`keysx/keys_` 被误识别的双重问题。恢复前端和 VM 无需修改。
- 新增 5 组专项测试：空 Map/中文排序/参数只求值一次、相似名称、含 Record 的 Map、错误参数数目与未定义前缀名称、非 Map 类型错误。合法源码同时逐字节比较两个前端产物。
- 自举初始与重建编译器同时覆盖 keys 正例和拒绝例，生产 acceptance 与 SDK 包内测试也覆盖准确名称识别。
- Windows 功能回归 76/76 通过（71.14 秒，排除独立性能门禁），B0=B1=B2 通过；解压 SDK 隔离 PATH 后，两套包内编译器的 keys/相似名称和全部既有安装检查通过。

### R1 核对与配置错误迁移（2026-11-23）

- 已将 REC-01 至 REC-13、REG-01 映射到真实测试，形成 [R1 验收记录](KU_RECORD_R1_ACCEPTANCE.md)。Schema/Result 项单独列为未实现，不用清单长度推算总体完成率。
- 配置校验器内部错误列表使用四标量 `校验错误` Record，返回前显式导出独立 Map。公开 `{ok,errors}`、四个错误字段、CLI 完整文本、退出码与顺序保持；原生配置用例由 9 个增至 12 个。
- 自举前端修复两项验收复现：兄弟导入共享可变祖先列表导致误报循环；字段与类型/函数同名时被命名空间错误改写。18 组前端用例验证双前端字节、两个别名类型身份、共享依赖和定位；自举重建也执行对应例子。
- 最终 Windows 功能回归 75/75 通过（71.24 秒，排除独立性能门禁）；B0=B1=B2 通过，独立 ASan/UBSan 的 Record 内核/工具/存储和配置 CLI 测试 4/4 通过。解压 SDK 隔离 PATH 后的配置 v3 模块、两个前端各 12 个配置测试、别名字段场景及既有绑定/Store 验证通过。
- 初次验收发现的自举 `keys(Map)` 名称长度差异已由上节兼容修复解决；配置测试保留 Map 遍历字段数的既有实现。跨平台和独立性能门禁仍未执行。

### R1 Python / Rust Record 绑定切片（2026-11-23）

- Python `Runtime(..., features=0)` 默认不启用 Record，提供能力查询/设置、模块 Record 描述、`record_type` 和 `record_get` 只读访问；owner 按 retained 模块引用显式释放。视图继续借用，不自动转 Map 或延长 generation。
- `CodeMemory(..., features=0)` 将显式开启策略传递到原生保存、派生保存、验证与执行。旧整数入口拒绝 Record 结果；普通调用拒绝复用句柄，Host 边界继续递归拒绝 Record。
- Rust sys crate 保持原始 C ABI 形态，增加 no_std、能力/值/状态常量和只读查询声明。新增实际原生调用探针，覆盖四种标量、输出清零、失效/外来句柄、owner 保活及策略锁定；不只依赖 cargo check。
- Windows 功能 CTest 75/75 通过（71.58 秒，排除独立性能门禁）；Python 7 组 Record 用例与 Rust 原生调用探针均通过。解压 SDK 隔离 PATH 后，包内 Python 查询/CodeMemory 和 Rust 系统绑定实际调用验证通过；本轮未验证 Linux/macOS。
- 下一步是 R1 总体验收核对与配置示例迁移，之后再开展 Schema/Result；完整静态类型与跨模块 Record 值签名仍不在本切片。用法见 [KU_RECORD_BINDINGS.md](KU_RECORD_BINDINGS.md)。

### R1 ModuleStore 能力策略切片（2026-11-23）

- `ModuleStore(root, features=0)` 默认拒绝 v3；显式传入 `DAO_FEATURE_RECORDS` 后允许校验、保存与读取具备身份的 Record 模块。未知能力位在构造时拒绝，没有全局开关或 manifest 新字段。
- `save/load/verify/save_source/save_derived_source` 使用同一策略，包含派生版本父记录的校验。完整性、来源与不可变规则保持；开启能力不绕过验证器，也不授权其他 Store 或执行 VM。
- `dao-ku --enable-records --store ...` 与 `dao-module --enable-records verify/run/recall ...` 接通原生闭环。CLI 拒绝输出 Record/容器句柄整数；实际执行 VM 仍单独显式启用能力。
- 存储不新增缓存、自动依赖链接、运行值序列化或递归父链加载。旧模块可继续默认保存/读取；跨模块仍仅允许既有安全值，不开放 Record 传递。
- Windows 功能 CTest 73/73 通过（排除独立性能门禁），存储 ASan/UBSan 测试 2/2 通过。解压 SDK 隔离 PATH 后，通过默认拒绝、双前端相同存储字节、显式 verify/run 和派生版本来源校验；外部 C/C++ consumer 的 Record Store API 验证通过。
- 下一步是 Python/Rust 的能力与只读查询绑定；完整 R1、Schema 与 Result/Option 尚未完成。使用规则见 [Store feature policy](../kernel/MODULE_ABI.md#store-feature-policy)。

### R1 ABI11 汇编工具切片（2026-11-23）

- 汇编/反汇编结构保留 format/ABI 版本、Record 类型和字段表；v3 文本使用 `format: 3 abi=11`、`record_types:` 描述和三条 Record 指令。支持空类型、可选模块身份/导入及共享 DATA 名称；旧 v1/v2 文本保持原样。
- v3 二进制反汇编与文本汇编结果均经临时启用 Record 的内核验证器检查，不链接依赖或执行程序。规范 ModuleBuilder 产物保持字节一致往返；不承诺保留任意非规范二进制布局。
- 数值 AOT 在打开输出前拒绝全部 v3 模块，包括只声明 Record、没有相关指令的模块；已有输出文件不被截断。
- 汇编、反汇编和新工具往返测试已通过独立 ASan/UBSan 检查；安装 SDK 的外部 C/C++ consumer 验证通过，C++ 示例只链接导出的汇编/反汇编目标即可完成 Record 字节往返。
- Windows 完整功能 CTest 71/71 通过（92.09 秒，排除独立 AOT 性能阈值）；独立 ASan/UBSan 的汇编、反汇编与 Record 工具测试 3/3 通过。解压 SDK 后隔离 PATH 验证通过，包括数值 AOT 拒绝与已有输出保护。
- 下一步是 ModuleStore 显式能力策略，再接高层绑定；当前不修改存储 manifest 或 Record 跨模块/Host 边界规则。格式见 [FORMAT.md](../kernel/FORMAT.md#abi11-assembly-text)。

### R1 Record 源码前端切片（2026-11-23）

- 两套前端已支持顶层 `型`、标量字段、前向声明、具名构造、只读字段读取和源码导入命名空间。具名初始化按源码顺序求值，按声明顺序存放；无 Record 程序沿用旧编码。
- 字段缺失/多余/重复、明显类型错误、已知 Record 的未知字段/索引/写入和类型名遮蔽在编译期拒绝；动态字段仍由 VM 严格检查。没有新增 Schema、Result/Option 或完整 Typed IR。
- Windows 功能 CTest 69/69 通过（排除独立 AOT 性能阈值）；前端专项 13 组测试检查字节一致、导入、求值顺序、重赋值/绑定、格式化与诊断。自举门禁维持 B0=B1=B2，并验证重建编译器生成的 Record 程序。
- `demos/record_tests.ku` 演示配置错误 Record 和显式导出 Map，包内双前端编译/执行和字节一致性已通过隔离 PATH 的 SDK 验证。源码编译器重建测试的指令预算由 100M 调至 200M；生产编译预算不变。
- 下一步是 ABI11 的汇编/反汇编、ModuleStore 能力策略与绑定接入；本切片不代表整个 R1 已完成。使用与限制见 [RECORDS.md](../kernel/RECORDS.md)。

### R1 Record 内核切片（2026-11-22）

- 已实现 opt-in v3/ABI11、Record 类型/字段表、严格标量构造、静态/动态字段读取、只读 C 查询和 capability 锁定。旧 v1/v2 编码与默认 VM 策略保持，源码 `型` 尚未接入；范围见 [RECORDS.md](../kernel/RECORDS.md)。
- 14 个 R0 字节样本已接生产 C ABI；原生测试覆盖跨 VM/下一代拒绝、module release/cache 清空后的描述保活、嵌套 Host/模块/异常边界、循环容器、边界预算和旧 ABI 回归。
- Windows 完整功能 CTest 66/66 通过（34.58 秒，排除独立性能阈值），新增图边界反例随后定向复测通过；独立 RelWithDebInfo + ASan/UBSan 的 Record 原生测试通过。本轮未运行 Linux/macOS。
- 候选 Windows SDK 解压后，使用包内动态库、移除开发 PATH 的环境再次通过全部 14 个 wire 样本；既有配置工具、编译诊断、代码记忆及 Python 绑定安装验证通过。未发布新版本。
- 下一步是两套源码前端与 ABI11 工具/存储/绑定接入。当前不能把内核切片视为整个 R1、Schema 或 Result/Option 已完成。

### Record / Schema / Result 设计提案（2026-11-22）

- 已形成 [最小契约 RFC](KU_RECORD_SCHEMA_RESULT_RFC.md)，建议独立名义 Record、固定只读字段、显式 Schema 转换与带标签的 Result/Option，并列出替代方案、兼容条件和配置校验器的迁移顺序。
- [R0 编码与能力协商候选附录](KU_RECORD_R0_ABI.md) 已形成，明确 v3/ABI11、类型/字段表、Record 三条指令、值标签、C API、owner 保活、能力检查顺序和边界拒绝规则，并附 252 字节基准及 14 个提案样本。
- R0 核对通过样本长度、section 表偏移、SHA-256、变异描述范围和文档链接；当前 `dao-aot` 拒绝该 v3/ABI11 样本且不创建输出文件。本轮未修改运行时或重跑完整 CTest；新协议的加载/执行预期仍待 R1 验证。
- R1 内核与源码切片实现见上节；跨模块类型签名、Schema 转换与 Result/Option 尚未实现。设计文档本身不作为新内核执行成功的证据。

### v1.2.0 开发中的真实工具示例（2026-11-22）

- 新增原生 `ku-config` 与 `demos/config_validator.ku`。C++ 负责文件输入、VM 调用和错误输出，Ku 负责 key=value 解析、必填项、重复键、未知键与数值范围；使用说明见 [KU_CONFIG_EXAMPLE.md](KU_CONFIG_EXAMPLE.md)。
- 原生测试与自举编译器共用三个只读文本 Host；没有增加文件或网络权限。九个 Ku 测试在默认与恢复前端通过，校验器产物逐字节一致，真实文件 CLI 回归通过。
- Windows 本地完整功能 CTest 64/64 通过（28.21 秒，排除独立性能阈值）。候选 SDK 在独立解压目录通过配置示例、双前端九个 Ku 测试、错误输入及包内模块损坏拒绝验证；既有诊断、代码记忆和 Python 绑定验证继续通过。本轮未执行 Linux/macOS 验证，未发布新版本。
- 示例使用显式中间变量读取调用结果和嵌套容器，避开当前前端对部分链式字段/索引访问的限制；本轮未扩展该语法。

### v1.2.0 开发中的编译诊断（2026-11-22）

- 两套前端的 CLI 编译错误支持文件、Unicode 行列、源码行与指示符；嵌套导入、函数名前缀和自动记忆声明保留原文件位置。使用契约见 [KU_DIAGNOSTICS.md](KU_DIAGNOSTICS.md)。
- 新增诊断 CLI 回归，覆盖中文/CRLF/Tab、语法、文件末尾、根文件与嵌套导入、导入失败、重复声明、循环外跳出、函数名重写、各源码命令与非法 UTF-8。
- Windows 本地完整功能 CTest 60/60 通过（30.11 秒，排除独立性能阈值）；其中诊断 CLI 的 14 组回归通过，双代自举与双前端字节一致性门禁通过。
- 候选 Windows SDK ZIP 在独立解压目录通过双前端诊断、原生测试、check/fmt/编译、代码记忆与 Python 绑定验证；运行时 PATH 不含源码工具链。本轮未运行 Linux/macOS 验证，也未发布新版本。
- 范围外发现并保留：自举前端使用较长源码导入别名时，`identifier_has_specific_alias` 可能在短标识符上调用越界的 `source_byte`，报 `host callback returned an error`。例如别名 `long_alias`，依赖源码为 `思 value() { 返 42 }`；本轮未修改该别名判断逻辑。

### v1.2.0 开发中的原生测试命令

- `dao-ku --test [--recovery] input.ku` 已实现单文件函数发现、独立 VM 执行、失败后继续和 0/1/2 退出码，使用 `测试_` / `test_` 前缀及严格 Trit 成功规则。
- 本地功能 CTest 59/59 通过（29.12 秒，排除独立性能阈值），包括 CLI 回归和中文示例。候选 SDK 解压后的双前端 `--test`、编译和代码记忆路径通过。
- 使用契约见 [KU_TESTING.md](KU_TESTING.md)。编译诊断与真实工具示例进展见上节，已发布版本仍以 Releases 为准。

### 2026-11-21 当前本地验收

- v1.1.0 发行收口：SDK、Python 项目和 Rust 绑定版本统一为 1.1.0。Windows ZIP 已在独立解压目录通过检查、格式化、编译、保存、验证、执行与 Python C ABI 验证，工具链路径从测试环境中移除。新增三平台包验证及版本标签发布任务，远端结果见 PR #30 和 Releases。
- 首页整理为下载、快速开始、当前能力及文档导航，历史根目录看板和早期记忆架构移入 `docs/archive/`。中文语法、源码构建和旧接入说明使用可展开入口。
- ModuleStore 单写入者契约审查完成本地验收，最新功能 CTest **57/57 通过**（28.86 秒），Python 集成测试 **21/21 通过**。
- schema-1 manifest 现按完整 JSON 校验，新增可选源码校验和；模块、来源与父版本保持不可变，父记录须存在，manifest 最后发布。已有记录缺失的源码可通过相同输入重试恢复。
- 同目录 native 写入使用进程级 OS 锁，重叠写入返回忙碌错误；进程退出释放锁。新增契约测试覆盖 I/O 失败、派生保存重试、旧格式兼容、Unicode 转义和锁释放；Python 适配器已覆盖临时输入隔离。
- 以下 55 项记录对应同日之前的修复；本轮跨平台验证结果以 PR #30 最新提交的 CI 为准。
- 增量构建成功；排除 `aot_performance_gate` 的功能 CTest **55/55 通过**（27.12 秒）。
- CI 选用的 Python/MCP/记忆/任务测试 **21/21 通过**，包含中文 Thought 参数持久化回归。
- 三个缺陷先经失败测试复现，再修复：相同模块保存时恢复缺失 manifest；严格拒绝非法版本号；中文 `思` 参数推断与英文兼容语法一致。
- 原生模块存储、标量记忆 Host、格式化及其测试作为开发分支的集成候选提交；这些证据不等于已发布版本。
- 已核对历史远端 CI：[run 31774870246](https://github.com/zhangshaoling/ku/actions/runs/31774870246) 的 Windows/Linux/macOS 和 `test` 均成功；它对应旧提交 `1db456a`，不能替代本轮改动的三平台验证。
- SDK 安装和 `tools/test_installed_sdk.ps1` 独立 CMake consumer 验证通过；这不等于已验证脱离源码树、系统工具链的完整发行包。
- 本轮没有执行性能阈值、全量旧链 pytest、fuzz 或 soak；完整发行验证仍需独立收口。

以下 2026-07 的测试记录保留为历史证据，不代表当前测试数量或未完成项状态。

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
| KU-P11 | Thought 与语义 IR | 进行中 | Python Thought 和原生 ModuleStore 已有编译/执行/持久化路径；完整 schema、语义 IR 与来源链契约待收口 |
| KU-P12 | 可执行记忆 | 进行中 | 文件记忆和跨进程标量 Host 已验证；生命周期、迁移、反馈排序和并发事务未闭合 |
| KU-P13 | 任务与工具闭环 | 进行中 | 依赖排序和 C ABI 执行可用；目标分解仍含占位实现，晋升/退役与 outcome 闭环未完成 |
| KU-P14 | 原生 Agent Gateway | 进行中 | Python Agent/MCP 可调用新内核；完整原生长期运行网关及取消、恢复未完成 |
| KU-P15 | 旧链迁移与退役 | 进行中 | 旧 CLI run 委托给新 kernel；repl/status 加 DEPRECATED；MCP server 加 Deprecation；迁移指南已发布 |
| KU-P16 | 质量、跨平台与发布 | 进行中 | 历史三平台 CI 成功；本轮 55 项本地功能测试通过，更新后的远端 CI 和发布门禁待验证 |

状态计数：

- `已完成`：9 项；
- `进行中`：8 项；
- `待迁移`：0 项；
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

### C. AI 机器语：新链已有闭环，完整语义继续迁移

新链已有 Python Thought/MemorySystem、原生 ModuleStore、标量记忆 Host 和 MCP 工具闭环。Agent 的工具选择仍以名称匹配为主，TaskPlanner 的目标分解仍包含占位 Thought；这些实现不代表完整语义 IR、模型规划或原生网关已完成。

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
3. **当前改动的 CI 待生成**：历史三平台 CI 已成功；本地未提交改动仍需在 PR 上重新验证，不能沿用历史绿灯。
4. **全量测试耗时**：本地 `pytest -q` 五分钟未结束，需要拆分慢测试、标记 soak 或定位异常耗时。
5. **AI 层只完成部分迁移**：新内核执行闭环已有证据，完整 Thought schema、记忆生命周期、工具反馈与原生长期运行网关仍未完成。
6. **Verifier 待复现审计项**：零长度 section 可能遮蔽相邻非空 section 重叠；属于 KU-P02，不由本次 typed-builder 切片代修。

## 6. 下一阶段计划

> 下列分批计划保留历史顺序；当前优先级为模块存储契约与恢复、未提交改动审查、PR CI 和安装发布验收。第三批各项均已启动，不能再视为从零开始，也不能仅凭 Python 桥接标记全部完成。

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

1. KU-P15：旧链入口已加 deprecation 警告；CLI run 已委托给新 kernel；repl/status 加 DEPRECATED 提示；MCP server 加 Deprecation（旧 .ku 用中文关键字，新 kernel 用英文，语法不兼容，待 P16 迁移）；迁移指南已发布。
2. KU-P16：完成多平台、fuzz、soak、benchmark、SDK 和语法包发布门禁。

## 7. 下一张工作单

> 当前工作单：`KU-P10-M6 / 代码模块存储与同步收口`，保持进行中。以下是 P09/P10 的历史工作单记录。

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
