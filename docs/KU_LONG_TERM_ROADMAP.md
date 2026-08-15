# Ku 长期发展目标与规划大纲

> 状态：长期目标基线
>
> Ku 的目标不是研发某一个通用模型，而是成为**模型无关、硬件无关、部署无关的智能系统通用编程语言**。
>
> 模型负责能力，Ku 负责结构、契约、流程、执行、错误、并发、验证与恢复。

## 1. 总目标

Ku 应统一编程以下对象：

- 普通数据与程序；
- 文本、视觉、语音、向量和推理模型；
- 模型组合与路由；
- 工具和外部环境；
- 并发任务与流式结果；
- 长期状态、知识、证据和反馈；
- 可追踪、可暂停、可恢复、可重放的执行过程；
- 经验证、可回滚的程序生成与自修改。

Ku 不应退化为：

- 某一个模型供应商的 SDK；
- Python 胶水代码的另一种写法；
- MCP 协议的内部实现；
- 以 JSON 形状代替类型系统；
- 以数据库代替语言值模型；
- 未经验证的字符串自执行框架。

## 2. 总体路线

```text
基础发布与路线冻结
  -> Ku v1 语言收口
  -> Record / Schema / Result
  -> Typed IR / 类型系统 / 模式匹配
  -> Model Capability ABI
  -> 流式、任务与并发模型
  -> 多模型编排
  -> 可追踪与可恢复执行
  -> 知识、记忆与反馈
  -> 安全代码生成与自修改
  -> 跨平台发行与生态
```

## 3. 阶段计划

### 阶段 0：基础发布与路线冻结

目标：把当前语言基础变成稳定公开基线。

任务：

- 合并并保护语言基础分支；
- 固定 Ku v1 语义、ABI 和测试基线；
- 严格区分语言本体与 Python/MCP/ModuleStore 外围；
- 建立语言变更 RFC 规则；
- 保持 recovery/selfhost parity 门禁；
- 建立模块 ABI 兼容策略；
- 完善干净 checkout 构建、测试和发布说明。

完成标准：

- 干净 checkout 可以构建；
- 核心 CTest 全部通过；
- recovery 与 selfhost 生成结果一致；
- 外围实验不会成为语言核心的隐式依赖。

### 阶段 1：Ku v1 语言收口

目标：让 Ku 能承担真实的普通编程任务。

任务：

- 补齐字符串分割、连接、解析和格式化；
- 统一结构化错误值；
- 完善编译错误的行列号、上下文和错误码；
- 实现最小 `ku check`、`ku fmt`、`ku test` 和 REPL；
- 补齐 List、Map、文本和配置处理常用能力；
- 完成标准库 ABI 登记和版本说明。

完成标准：

Ku 可以独立编写命令行工具、配置解析器、文本处理器、数据转换器、小型编译器和测试程序。

### 阶段 2：Record / Schema / Result

目标：从 `Map + object.field` 发展为真正的结构化值模型。

示意：

```ku
型 用户 {
  姓名: 文本
  年龄: 整数
}

用户实例 = 用户 {
  姓名: "小明"
  年龄: 20
}
```

必须冻结：

- Record 是否为独立值类型；
- Record 与 Map 的边界；
- 字段是否固定、可变和可嵌套；
- 字段缺失行为；
- Record 是否可跨模块；
- Record 是否可放入 List/Map；
- Record 序列化与版本演化；
- Record 的 ABI 编码与所有权。

同时设计：

- `Result<T, E>`；
- `Option<T>`；
- 统一错误和结果流。

暂不优先：继承、复杂反射、隐式结构转换和自动 ORM。

### 阶段 3：Typed IR、类型系统与模式匹配

目标：让 Ku 成为可验证的结构化语言。

编译器路线：

```text
源码
  -> AST
  -> 名称解析
  -> 类型检查
  -> Typed IR
  -> lowering
  -> Register VM 字节码
```

任务：

- 稳定 AST；
- 名称解析和类型环境；
- Record、Union/Sum、Option、Result 类型；
- 类型推导；
- 参数、返回值和字段检查；
- 模式匹配、解构绑定和完备性检查；
- 统一类型诊断。

目标：编译期发现字段不存在、参数类型错误、返回类型错误、模式不完整和未处理 Result 等问题。

### 阶段 4：Model Capability ABI

目标：让所有模型通过统一 Ku 接口使用，而不是将供应商 API 写死进语言。

模型能力包括：

- 文本生成；
- 结构生成；
- 向量化；
- 图像理解与生成；
- 语音识别与合成；
- 重排序、分类、评分和规划；
- 工具调用；
- 流式生成。

模型接口需要描述：

- 模型身份和版本；
- Provider；
- 输入、输出 Schema；
- 能力集合；
- 上下文窗口；
- 流式能力；
- 资源成本；
- 硬件需求；
- 取消和超时能力。

统一模型结果应覆盖：

- 成功；
- 失败；
- 取消；
- 超时；
- 限流；
- 上下文超限；
- 结构解析失败。

同一 Ku 程序应能替换云端模型、本地模型、大小模型和不同硬件后端，而不改写业务流程。

### 阶段 5：流式、任务与并发模型

目标：统一处理模型流式输出和多个异步能力。

建议模型：

- Task；
- Stream；
- Event；
- Future；
- Actor；
- 消息传递；
- 结构化并发；
- 显式取消；
- 超时和资源预算；
- 并发错误传播。

不直接复制 Python 的线程、全局可变对象和 `asyncio` 拼接模型。必须先冻结：

- 任务所有权；
- generation 是否可跨任务；
- 容器共享规则；
- 任务取消和错误传播；
- Host 阻塞边界；
- 调度确定性；
- VM 与 AOT 预算一致性。

### 阶段 6：多模型编排

目标：让 Ku 能够组合不同类型、不同 Provider 和不同硬件上的模型。

典型流程：

```text
语音识别
  -> 问题提取
  -> 知识检索
  -> 文本生成
  -> 结构验证
  -> 语音合成
```

需要支持：

- Pipeline；
- 模型路由；
- 模型回退；
- 并行调用；
- 结果合并；
- 多模型投票；
- 输出 Schema 验证；
- 成本、延迟和质量控制；
- 失败分支与重试。

业务代码保持不变，模型绑定、Provider 和硬件后端可以替换。

### 阶段 7：可追踪与可恢复执行

目标：让长时间模型流程能够暂停、保存、恢复、重放和回滚。

任务：

- Trace、Span、Event；
- Checkpoint；
- Replay；
- Retry；
- Deterministic reentry；
- 状态快照；
- 任务恢复；
- 执行差分；
- 模型版本、输入、输出和资源记录；
- 外部副作用记录与回滚策略。

目标流程：

```text
开始
  -> 模型调用
  -> 工具调用
  -> 观察结果
  -> 保存 checkpoint
  -> 继续或恢复
```

### 阶段 8：知识、记忆与反馈

目标：形成统一的长期状态能力，但不让数据库或协议定义语言语义。

核心对象：

- Fact；
- Evidence；
- Hypothesis；
- Belief；
- Observation；
- Experience；
- Feedback；
- Skill。

必须支持：

- 来源、时间和版本；
- 置信度；
- 冲突处理；
- 召回和解释；
- 反馈更新；
- 过期、删除和迁移；
- 经验复用。

正确分层：

```text
Ku 语言
  -> 可追踪执行
  -> 持久状态接口
  -> 记忆实现
```

### 阶段 9：安全代码生成与自修改

目标：允许程序生成、验证、编译和替换程序，但不能绕过安全边界。

任务：

- 稳定 AST；
- Quote / Unquote；
- 语法树操作；
- 类型和权限检查；
- 资源限制；
- 沙箱；
- 差分、版本、审计和回滚。

目标流程：

```text
发现缺口
  -> 生成补丁
  -> 解析
  -> 类型检查
  -> 编译
  -> 运行测试
  -> 比较结果
  -> 批准或回滚
```

### 阶段 10：跨平台发行与生态

目标：让 Ku 成为可安装、可发布、可维护的完整语言平台。

任务：

- Windows、Linux、macOS；
- CPU、GPU、NPU、云端和边缘设备；
- SDK、包仓库和依赖锁定；
- IDE/LSP、调试器和性能工具；
- 文档站和示例工程；
- 版本管理、安全更新和兼容性测试。

## 4. 优先级

### P0：立即

```text
v1 规范收口
标准库收口
编译器诊断
Record / Schema RFC
```

### P1：语言核心

```text
Record
Result / Option
Typed IR
模式匹配
类型检查
```

### P2：模型通用化

```text
Model Capability
Provider 抽象
Schema 驱动输出
统一 ModelResult
流式结果
```

### P3：运行时跃迁

```text
Task
Stream
Actor
取消、超时和资源预算
多模型编排
```

### P4：智能系统能力

```text
Trace / Replay
Checkpoint
持久状态
证据和反馈
安全自修改
```

## 5. 依赖关系

```text
v1 收口
  -> Record / Schema
  -> Result / Option
  -> Typed IR
  -> 模式匹配
  -> Model Capability ABI
  -> 统一模型结果
  -> Task / Stream / Cancellation
  -> 多模型编排
  -> Trace / Replay / Checkpoint
  -> 知识、记忆与反馈
  -> 安全自修改
```

不应倒置：

- 没有 Schema，不先冻结稳定模型 ABI；
- 没有 Task，不先做复杂流式编排；
- 没有 Trace，不先做长期任务恢复；
- 没有 AST/Typed IR，不先做安全自修改；
- 没有 Record，不先做复杂认知对象。

## 6. 90 天近期计划

| 周期 | 任务 | 交付物 |
|---|---|---|
| 第 1–2 周 | 合并语言基础、冻结 v1 | 稳定主分支 |
| 第 3–4 周 | 标准库、错误和诊断收口 | string/error/parse 基础 |
| 第 5–6 周 | REPL、fmt、check 设计与最小实现 | 开发工具初版 |
| 第 7–8 周 | Record/Schema RFC | 值模型规范 |
| 第 9–10 周 | Record parser/lowering | Record 编译支持 |
| 第 11 周 | Record VM、ABI、所有权 | Record 执行支持 |
| 第 12 周 | Record recovery/selfhost parity | 第一切片验收 |
| 第 13 周 | Result/Option 设计 | 类型系统阶段入口 |

## 7. 最终分层

```text
Ku 程序层
  -> 普通函数
  -> Record / Schema
  -> Result / Option
  -> 模式匹配
  -> Pipeline
  -> Task / Stream
  -> Model Capability
  -> Trace / Replay
  -> Knowledge / Evidence
  -> 安全代码生成

Ku 语义层
  -> AST
  -> Typed IR
  -> Capability IR
  -> Task IR
  -> Trace IR
  -> Module IR

Ku 执行层
  -> Register VM
  -> Verifier
  -> Module ABI
  -> Ownership
  -> Scheduler
  -> Checkpoint
  -> AOT

外部资源层
  -> 各类模型
  -> CPU / GPU / NPU
  -> 文件系统和数据库
  -> 网络与工具
  -> 外部环境
```

## 8. 核心原则

```text
模型不决定语言
供应商不决定 ABI
JSON 不决定值模型
MCP 不决定内部语义
Python 不作为生产权威
记忆不替代类型系统
自修改不绕过验证
```

最终目标：

```text
任何模型
+ 任何硬件
+ 任何供应商
+ 任何知识源
+ 任何工具
+ 任何部署方式
  -> 都可以由 Ku 统一描述、组合、验证、执行和恢复。
```
