# Ku 语言系统小项目工作表

> 建立日期：2026-07-14  
> 适用范围：Ku 语言、Ku 新内核、旧运行时迁移、AI 机器语上层和工程化  
> 产品命名：**Ku 是语言系统名称**。仓库中的 `dao_*`、`Dao Binary Module`、`dao/`
> 等名称暂作为现有实现标识保留，是否统一重命名必须另立项目，不能在功能开发中顺手修改。

## 1. 工作规则

每次只选择一个小项目作为主项目。一个小项目只有同时满足以下条件才能标记为完成：

1. 边界明确：写清本项目负责什么、不负责什么。
2. 契约明确：输入、输出、错误和稳定接口有文档或测试定义。
3. 实现落地：生产路径不存在未声明的旧实现兜底。
4. 自动验证：验收命令可重复运行并产生确定结果。
5. 集成完成：直接上游和下游至少各有一条集成证据。
6. 状态诚实：跨平台、性能或发布未验证时，不得用“基本完成”代替。

状态统一使用：

- `待开始`：只有想法或依赖尚未满足。
- `待迁移`：旧链已有能力，新内核生产链尚未拥有。
- `进行中`：已有实现，但未满足全部验收条件。
- `待验收`：实现结束，正在补齐完整证明。
- `已完成`：验收条件全部通过。
- `受阻`：依赖、设计决策或外部条件阻止继续推进。

### 本地资源与温控规则

- 默认使用低并行度构建；未特别说明时将并行任务限制在 2 个以内。
- 不同时运行全量编译、全量测试、fuzz、benchmark 或 soak 等多个重负载任务。
- 先运行静态检查和针对性测试，只有风险需要时才扩大验证范围。
- 30 分钟、10,000 请求、100,000-input fuzz 等长任务必须单独安排并分段汇报。
- 文档和规划任务不触发无关构建；能够复用当日有效测试证据时不重复满载运行。
- 若风扇持续高转或机器明显发热，停止非必要重任务，保留结果并改为分批验证。

## 2. 总依赖图

```text
KU-P00 产品身份与权威文档
  ├─> KU-P01 Binary Module / Bytecode ABI
  │     └─> KU-P02 Loader 与 Verifier
  │           └─> KU-P03 Register VM
  │                 └─> KU-P04 值、容器与内存所有权
  │                       ├─> KU-P05 C ABI、FFI 与绑定
  │                       │     └─> KU-P06 Cache、AOT 与 SDK
  │                       └─> KU-P11 Thought 与语义 IR
  └─> KU-P07 Ku 语言规范与一致性语料
        └─> KU-P08 恢复/迁移编译器
              └─> KU-P09 Ku 自举编译器
                    └─> KU-P10 模块系统与标准库
                          └─> KU-P11 Thought 与语义 IR
                                └─> KU-P12 可执行记忆
                                      └─> KU-P13 任务与工具闭环
                                            └─> KU-P14 原生网关

KU-P15 旧链迁移与退役：横跨 KU-P08 至 KU-P14
KU-P16 质量、跨平台与发布：横跨全部项目
```

## 3. 内核层工作表

### KU-P00：产品身份与权威文档

- 状态：`已完成`
- 目标：确立 Ku 为唯一产品/语言名称，解释现有 Dao 实现名，不再让两套名称产生两套路线。
- 范围：README、文档入口、术语表、路线权威顺序、目录角色。
- 不包含：批量重命名 C ABI、二进制格式、目录、包名和历史文件。
- 交付物：名称决策记录、权威文档索引、历史文档失效标记。
- 验收：新读者能从一个入口回答“什么是 Ku、当前权威实现在哪里、旧链为何保留”。
- 依赖：无。
- 完成证据：`KU_NAMING_AND_AUTHORITY.md` 已建立；README、母语说明、项目宪法、项目结构、内核指南和内核 README 已统一入口口径。

### KU-P01：Binary Module 与 Bytecode ABI

- 状态：`已完成`
- 目标：提供确定性、版本化、可校验的 Ku 生产模块格式和数值指令 ABI。
- 范围：`kernel/FORMAT.md`、`kernel/include/dao/format.hpp`、模块编码器和格式测试。
- 不包含：源语言语法、MCP、记忆语义。
- 验收：确定性编码；assembler/disassembler 字节一致往返；ABI 版本和边界检查稳定。
- 证据：2026-07-14 新内核构建通过，相关 CTest 包含 conformance、assemble、disassemble。
- 依赖：KU-P00。

### KU-P02：Loader 与 Verifier

- 状态：`已完成`
- 目标：任何模块必须先严格验证，再进入执行；畸形输入不能越界、静默降级或污染状态。
- 范围：`kernel/src/runtime.cpp` 的加载/验证边界、fuzz 入口、错误码。
- 不包含：高级语言诊断和业务级权限策略。
- 验收：边界、指令、函数、导入导出和分支目标均严格验证；fuzz 目标可运行。
- 证据：2026-07-14 `kernel_conformance` 通过；fuzz 代码存在，但本轮未运行 100,000 输入门禁。
- 依赖：KU-P01。

### KU-P03：Register VM 与核心执行语义

- 状态：`已完成`
- 目标：稳定执行数值寄存器字节码，包括 i64、Trit、分支、调用、预算和结构化异常。
- 范围：解释器、函数调用、寄存器规则、指令预算、核心错误语义。
- 不包含：完整 Ku 动态语言表面和上层 AI 语义。
- 验收：生产执行不解析源码；数值 opcode；热路径不依赖 Python；核心测试通过。
- 证据：2026-07-14 新内核 39/39 CTest 通过。
- 依赖：KU-P02。

### KU-P04：值、容器与内存所有权

- 状态：`已完成`
- 目标：冻结字符串、bytes、List、Map、函数引用和 Host 容器的所有权与生命周期。
- 范围：`dao_value`、borrowed view、VM-owned container、generation handle、调用 arena。
- 不包含：SQLite 记忆生命周期。
- 完成：字符串/bytes 使用显式 borrowed view；List/Map/Closure 使用单 VM、单顶层调用 generation arena；Function 保持 module-local。
- 完成：Host-created List/Map API 纳入稳定 C ABI；跨调用 retain、跨调用 Closure 和 Host 制造 callable 被明确拒绝。
- 验收：所有值类型有明确 owner；跨 ABI 不悬空；ASan/UBSan 和长运行测试通过。
- 依赖：KU-P03。
- 完成证据：`kernel/OWNERSHIP.md`；2,048 次 generation 回收测试；2026-07-14 定向 CTest、C/C++ ABI、迁移/任务队列测试和 ASan/UBSan 构建通过。

### KU-P05：C ABI、FFI 与语言绑定

- 状态：`进行中`
- 目标：让任意智能体或宿主通过稳定 C ABI 嵌入 Ku，不依赖 JSON 中转。
- 范围：`kernel/include/dao/dao.h`、Host import、错误码、Python/Rust 参考绑定。
- 不包含：MCP 协议本身和业务 Host 模块。
- 已有：纯 C/C++ ABI smoke、数值 Host FFI、Python/Rust 绑定检查。
- 剩余：容器 Host ABI 稳定化；更多语言绑定仅在 C ABI 冻结后添加。
- 验收：C ABI 兼容性测试；Host 错误可传播；绑定保持薄层且无语义分叉。
- 依赖：KU-P04。

### KU-P06：Module Cache、AOT 与 SDK

- 状态：`已完成`
- 目标：交付可缓存、可安装、可选择 AOT 的 Ku 执行 SDK。
- 范围：verified module cache、portable-C AOT、CMake SDK、打包和性能门禁。
- 不包含：AOT 容器语义，除非 KU-P04 先冻结契约。
- 已有：cache、预解码、数值/Trit AOT、SDK 和参考绑定；本轮 AOT 测试通过。
- 剩余：容器 AOT 明确支持或明确长期拒绝；正式发布包和多平台制品验证。
- 验收：解释器/AOT 语义一致；性能门禁通过；安装后的外部 consumer 可编译运行。
- 依赖：KU-P05。

## 4. Ku 语言层工作表

### KU-P07：Ku 语言规范与一致性语料

- 状态：`已完成`
- 目标：把 Ku 的语法、值、作用域、错误、模块和 Thought 语义从实现中提取为规范。
- 范围：规范文档、规范示例、正反例语料、兼容级别。
- 不包含：为了兼容旧代码而无限复制 Python 语义。
- 已有：`KU_V1_SEMANTICS.md` 已成为源语言权威，区分核心、兼容、推迟和实验层，并记录恢复/自举支持差距。
- 完成证据：2026-07-14 语义矩阵门禁 8/8 通过；新增中文别名、Trit 非短路、严格条件、负索引和浮点拒绝语料；浮点、同调用绑定、持久闭包和模块边界已有显式裁决。
- 验收：每条规范至少有一个编译/执行测试；冲突行为有显式裁决。
- 依赖：KU-P00、KU-P01。
- 下一任务：KU-P08 按矩阵审计恢复编译器，不扩展已推迟语义。

### KU-P08：恢复/迁移编译器

- 状态：`已完成`
- 目标：用 C++ 恢复编译器把现有 `.ku` 转成已验证 Binary Module，作为迁移和自举种子。
- 范围：`kernel/src/ku_migration.cpp`、`dao-ku`、迁移测试。
- 不包含：成为最终语义权威。
- 已有：表达式、控制流、函数值/同调用绑定、容器、异常、显式 Host import 和受限源码模块导入。
- 完成证据：2026-07-14 Ku v1/旧 parser/task queue/自举交叉门禁 12/12 通过；修复零参数调用规范编码、`INT64_MIN`、重复参数和严格 UTF-8 输入边界。
- 验收：选定 Ku v1 语料全部编译、验证、执行并与规范一致。
- 依赖：KU-P01 至 KU-P05、KU-P07。
- 后续 KU-P09 已按同一矩阵完成自举与 canonical identity。

### KU-P09：Ku 自举编译器

- 状态：`已完成`
- 目标：最终语言前端由 `.ku` 实现，C++ 仅保留恢复/引导能力。
- 范围：`kernel/selfhost/compiler.ku`、typed-builder Host ABI、自重建测试。
- 不包含：把 loader、verifier、VM 或 C ABI 改写成 Ku。
- 已有：SH0、SH1、SH2 确定性自重建；UTF-8 标识符、中文核心别名、`push`、函数值/同调用 `bind`、`else if`/`否 若`、`//`/`;;` 注释和零参数规范编码已通过双代 parity。
- 本轮证据：2026-07-14 `ku_selfhost_seed` 通过函数值与中文语料；2026-07-15 英文/中文三路径链式分支及递归模块语料通过 bootstrap 与 rebuilt compiler，并保持连续重建字节一致。
- 生产证据：2026-07-15 递归导入及绝对路径、越界、缺失、循环定向 CTest 5/5 通过；加入精确诊断与 structured-builder 门禁后，排除独立性能阈值的新内核功能 CTest 44/44 通过。
- 诊断证据：2026-07-15 声明、参数、容器、调用、控制流和名称解析的 10 类精确 offset 在 bootstrap/rebuilt 双代通过；生产 `dao-ku` 精确 stderr 门禁通过。
- builder 证据：2026-07-15 生产 adapter 状态机、字段范围、patch、冻结、重复 encode 生命周期和返回前 Loader/Verifier 校验已落地；`ku_selfhost_builder` 覆盖拒绝矩阵及执行 42 正例，双代自举保持通过。
- canonical 证据：2026-07-16 typed-builder 在 finish 时收窄到精确寄存器数；自举 lowering 与 recovery 对齐；`ku_selfhost_seed` 要求全部正例逐字节一致及 `B0 == B1 == B2`；生产 `ku_compile_acceptance` 对默认/`--recovery` 产物做精确文件比较。
- module-path import 小切片：
  1. `P09-M1` `已完成`：`dao/selfhost.hpp` 冻结 resolver Host 边界；Host 只在模块根内解析路径并返回 UTF-8 源码。
  2. `P09-M2` `已完成`：`.ku` 前端实现依赖优先源码组合、`alias_function` 重写、递归导入和活动路径循环检测，内存 resolver 双代语料通过。
  3. `P09-M3` `已完成`：`dao-ku` 默认执行 `.ku` 前端；递归正例及绝对路径、越界、缺失、循环反例通过；旧兼容语料只能显式使用 `--recovery`，无自动 fallback。
- 验收：recovery bootstrap、首次重建和二次重建字节一致；规范语料在 bootstrap、rebuilt 与 recovery 上同结果且同字节；生产默认/恢复入口同源产物一致。
- 依赖：KU-P08。

### KU-P10：模块系统与标准库迁移

- 状态：`已完成`
- 目标：Ku 模块拥有稳定身份、显式能力边界和可版本化标准库，不依赖源码拼接。
- 范围：module identity/import/export、`kernel/stdlib/*.ku`、Host capability modules。
- 不包含：把 SQLite、HTTP、文件策略硬编码进内核。
- 已有：core、math、list、type、fs、io、debug、string、http、lexer 子集及测试。
- P10-M1 `已完成`：Dao Binary Module v2 / VM ABI v10 增加显式 UTF-8 identity、精确版本、module import 与 `CALL_MODULE`；VM-local linker 覆盖冲突、签名、缺失依赖、循环和模块局部值边界；v1/ABI9 保持可加载。
- P10-M2 `已完成`：`dao-ku --identity` 已直接向 recovery/selfhost builder 传递身份，不再反汇编重建；`ku:std/core@1.0.0` 通过默认/recovery canonical 编译与身份读取。
- P10-M3 `已完成`：recovery 与 `.ku` 自举前端均支持 `ku:IDENTITY@MAJOR.MINOR.PATCH` runtime import、懒 `MODULE_IMPORT` 去重和 `CALL_MODULE`；普通相对 import 继续源码组合。
- 首条 std 二进制边 `ku:std/list@1.0.0 -> ku:std/type@1.0.0/is_list(1)` 已闭合：provider 未链接返回 `DAO_IMPORT_NOT_FOUND`，链接后完整 list 测试通过；list 默认/recovery 产物逐字节一致。
- P10-M4 `已完成`：`lexer -> string` 与 `io -> fs/string` 全部改为精确版本二进制依赖；测试覆盖依赖逐步链接、缺失 provider、正常调用和 callable 跨模块拒绝。
- P10-M5 `已完成`：`STDLIB_ABI.md` 冻结 10 个 `ku:std/*@1.0.0` identity、依赖 export、公开 arity 与值边界；全部模块以 identity 编译并随安装包发布，typed-builder helper 保持在 selfhost 内部 ABI。
- 本轮证据：2026-07-16 低并发构建和安装 smoke 通过；排除独立抖动型 `aot_performance_gate` 的功能 CTest 47/47 通过（25.93 秒）。
- 推迟项：包仓库/版本范围、模块全局状态、热替换、私有 export 语法及 callable 跨模块协议不属于 v1 ABI；callback 型 list/debug API 保留源码组合语义并已明确记录。
- 验收：模块可单独编译/验证/缓存；循环和越界 import 被拒绝；标准库公开 API 有版本和测试。
- 依赖：KU-P07、KU-P08、KU-P09。

## 5. AI 机器语层工作表

### KU-P11：Thought 对象与语义 IR

- 状态：`待迁移`
- 目标：让 Thought 同时具有源码、结构、可执行入口、持久身份和派生关系。
- 范围：Thought schema、semantic environment、trace、patch、provenance。
- 不包含：先做通用 Agent framework。
- 旧链证据：`dao/std/semantic_core.ku`、`trace.ku`、`patch.ku` 和相关 Python/C VM 对拍存在。
- 剩余：在新模块 ABI 上定义 Thought；消除旧 C VM/Python 对象作为语义权威。
- 验收：一个 Thought 可编译、检查、执行、序列化定位并生成可验证派生版本。
- 依赖：KU-P04、KU-P07、KU-P10。

### KU-P12：可执行记忆存储、召回与生命周期

- 状态：`待迁移`
- 目标：实现 `thought = code = memory` 的持久化闭环。
- 范围：经验、数据、图、地址、FTS 召回、留存、迁移、压实、可信度和版本。
- 不包含：在没有文本召回闭环前先引入强绑定 embedding。
- 旧链证据：SQLite/FTS、稳定 locator、召回解释、promotion 已有 C VM-backed 原型和测试。
- 剩余：新内核 Host 模块；schema version/migration；留存/归档/压实；反馈驱动排序。
- 验收：记录、重启、召回、执行、反馈、再召回全链可重复；数据库升级不丢数据。
- 依赖：KU-P11、KU-P05。

### KU-P13：任务、工具晋升与反馈闭环

- 状态：`待迁移`
- 目标：把缺口转成任务，把可信程序记忆晋升为工具，并让调用结果反向更新记忆。
- 范围：task queue、gap-to-task、promotion/demotion、调用 outcome、trust 更新。
- 不包含：模型自行猜测成功/失败；outcome 契约必须明确。
- 旧链证据：任务队列、动态 promoted tool、MCP 调用原型已有测试。
- 剩余：新内核执行路径；统一 outcome；自动退役；可信度算法与并发事务。
- 验收：`gap -> task -> thought/tool -> execution -> outcome -> memory update` 有端到端测试。
- 依赖：KU-P12。

### KU-P14：原生 Ku Agent Gateway

- 状态：`待迁移`
- 目标：通过原生长期运行网关暴露 Ku Thought、记忆和工具，不依赖 Python 语义服务器。
- 范围：MCP/JSON-RPC framing、schema discovery、取消、超时、资源限制和恢复。
- 不包含：把协议逻辑放进 VM 内核。
- 旧链证据：Python stdio 网关默认调用旧 C VM；相关 55 项测试于 2026-07-14 通过。
- 剩余：新 C ABI 后端；原生 discovery；生命周期刷新；跨平台长期运行验收。
- 验收：记录—召回—执行—工具调用—记忆更新无 Python 语义依赖；stdout/stderr 严格分离。
- 依赖：KU-P05、KU-P10、KU-P12、KU-P13。

## 6. 收口与交付工作表

### KU-P15：旧链迁移与退役

- 状态：`进行中`
- 目标：把 `dao/` 旧 C VM、Python runtime、`ku/` 兼容实现逐项变成语料、桥接或可删除资产。
- 范围：能力清单、parity 映射、迁移开关、弃用说明、删除门禁。
- 不包含：在迁移完成前大删旧代码。
- 验收：每个旧模块只有四种明确状态：已迁移、兼容保留、测试夹具、可删除；生产路径无双重权威。
- 依赖：KU-P08 至 KU-P14。
- 下一任务：建立旧文件到新小项目的迁移矩阵。

### KU-P16：质量、跨平台、性能与发布

- 状态：`进行中`
- 目标：建立一个能证明 Ku 可发布的统一门禁。
- 范围：kernel CTest、legacy parity、sanitizer/fuzz、soak、benchmark、CI matrix、SDK/grammar 发布。
- 不包含：用单一“pytest 全绿”替代各层门禁。
- 已有：本地新内核 39/39 CTest；旧 C VM/记忆相关 55 项测试；AOT 性能门禁；新 Kernel Windows/Linux/macOS CI matrix 已配置，统一限制为 2 路并行和 20 分钟超时。
- 本轮证据：2026-07-14 workflow YAML 静态解析通过；Windows 使用仓库托管工具链，Linux/macOS 使用 runner 原生工具链；旧 C VM portability job 保持不变。
- 剩余：取得远端三平台新内核运行证据；完整 pytest 超时治理；fuzz/soak 定期门禁；正式发布演练。
- 验收：受支持平台可从干净 checkout 构建、测试、打包；每个失败能定位到具体层。
- 依赖：横跨全部项目。

## 7. 每次迭代的工作单模板

```markdown
# KU-Pxx / 迭代名称

- 目标：
- 当前前提：
- 明确不做：
- 输入契约：
- 输出契约：
- 允许修改的文件：
- 验收命令：
- 成功标准：
- 风险和取舍：
- 实测结果：
- 剩余问题：
- 状态：待开始 / 待迁移 / 进行中 / 待验收 / 已完成 / 受阻
```

## 8. 推荐执行顺序

1. KU-P08：`已完成`，恢复编译器一致性矩阵已闭合。
2. KU-P09：`已完成`，自举同矩阵与 canonical identity 已闭合。
3. KU-P10：下一工作单，完成模块 ABI 和标准库边界。
4. KU-P11 至 KU-P14：把 AI 机器语闭环迁到新内核。
5. KU-P15：逐项退役旧链。
6. KU-P16：继续贯穿执行，并在发布前完成跨平台总验收。
