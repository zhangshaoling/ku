# 可执行记忆核（Executable Memory Kernel）技术架构

## 一、现状诊断

| 维度 | 当前状态 | 问题 |
|------|---------|------|
| Schema | `confidence`, `trust`, `success_count`, `failure_count`, `recall_count` 字段已存在 | 运行时几乎不更新，字段成为摆设 |
| 决策逻辑 | `tiandao_reason` 硬编码返回 `"推理完成: " + 输入` | 无真实决策，无贝叶斯推断 |
| 反馈环 | MCP 调用后无回调更新计数 | 工具调用与记忆状态完全断裂 |
| 衰减 | `experience_decay` 仅做 `trust *= 0.99` | 无时间衰减因子，无指数衰减 |
| 召回排序 | `ORDER BY trust DESC, confidence DESC` | 纯静态排序，未加权 recency |
| 晋升/降级 | `memory_promote` 需手动调用 | 无自动判定，无阈值调度 |

---

## 二、核心设计

### 2.1 贝叶斯 Trust 更新公式

将 `trust` 建模为 **Beta 分布**的期望值，而非简单线性加减：

```
α = success_count + 1   （先验成功 + 观测成功）
β  = failure_count + 1  （先验失败 + 观测失败）
trust = α / (α + β)
confidence = 1 - (1 / (α + β))   // 样本越多，confidence 越接近 1
```

**更新规则：**

| 事件 | α 变化 | β 变化 |
|------|--------|--------|
| MCP 工具调用成功 | +1 | 不变 |
| MCP 工具调用失败 | 不变 | +1 |
| 经验被召回且任务完成 | +1 | 不变 |
| 经验被召回但任务失败 | 不变 | +1 |

**先验选择**：初始值 `success_count=1, failure_count=1`，对应 `trust=0.5`（无信息先验）。

> 存疑：是否需要引入"时间衰减先验"——让旧样本权重随时间降低？当前设计靠定期衰减函数补偿，但贝叶斯框架下可直接让 α, β 每期乘衰减因子 ρ<1。

---

### 2.2 召回排序函数（Recency × Recall 衰减）

排序不再是纯 SQL `ORDER BY`，而是**加权得分**：

```
score = trust × confidence × recency_factor × recall_boost

recency_factor = exp(-λ × days_since_last_recalled)
recall_boost   = 1.0 + min(recall_count, 5) / 5.0   // 上限 2.0
```

**参数建议：**

| 参数 | 值 | 含义 |
|------|-----|------|
| λ（衰减率） | 0.05 | 约 14 天衰减到 0.5 |
| recall_boost 上限 | 2.0 | 防止高频项垄断 |

SQL 层先把 trust/confidence 排序拉到候选集（减少计算量），道语言层再做精排。

---

### 2.3 MCP 工具调用反馈环

当前 MCP 调用路径：`mcp_server.py` → 工具执行 → 返回结果，**中间无记忆层介入**。

**设计：在 MCP server 中插入记忆代理层**

```
MCP 调用请求
    ↓
[代理层] 记录调用前状态 (tool_record_call)
    ↓
执行工具
    ↓
[代理层] 解析结果 → 判定 success/failure
    ↓
[代理层] 更新 tool_registry (success_count/fail_count)
    ↓
[代理层] 反向更新 source_experience 的 trust
    ↓
返回结果
```

**关键函数（新增）：**

- `mcp_call_with_feedback(tool_name, params)`：封装标准 MCP 调用 + 反馈
- `mcp_feedback(tool_name, success, experience_id?)`：更新计数并反向传播 trust

> 存疑：success/failure 的判定标准是什么？建议由调用方显式传入 `outcome` 字段，代理层不自行推断。

---

### 2.4 跨模型版本持久化

**原则：记忆不依赖 embedding 空间、不依赖特定模型版本。**

当前已满足的基础：
- 记忆存储在 SQLite，字段为结构化文本（topic, context, tags_json）
- 无 embedding 向量依赖

**显式保障设计：**

| 措施 | 说明 |
|------|------|
| `schema_version` 字段 | 标记记忆创建时的 schema 版本，未来迁移时可识别 |
| 语义字段全部文本化 | 不引用模型侧"概念 ID"，只用人类可读的 name/definition |
| 标签作为唯一跨层契约 | `tags_json` 使用固定命名空间，不依赖外部 ontology |
| 工具输入输出纯文本 | `params_json` / `output` 存原文，不引用中间表示 |

> 存疑：未来若引入 embedding 辅助召回（如向量相似度），如何保证旧记忆在新 embedding 空间仍可用？建议保留 FTS5 文本召回为主通道，embedding 仅作为可选 boost。

---

### 2.5 晋升/降级自动判定

| 操作 | 触发条件 | 动作 |
|------|---------|------|
| **经验→工具**（晋升） | `trust ≥ 0.75` AND `recall_count ≥ 3` AND `success_count / (success_count + failure_count) ≥ 0.6` | 调用 `memory_promote`，写入 `memory_promotion` 表 |
| **工具→经验**（降级） | `fail_count ≥ 3` AND `trust < 0.3` | 调用 `tool_demote`，`status='demoted'` |
| **经验归档** | `confidence < 0.15` 持续 30 天未召回 | `quality_status='archived'`，不再参与召回 |

**自动调度时机：**
- 每次任务完成后（`experience_validate` 之后）
- 每次 MCP 调用反馈后
- 周期性衰减检查时（会话启动或定时器）

---

### 2.6 模块交互图

```
┌─────────────────────────────────────────────────────┐
│                    MCP Server Layer                   │
│  mcp_call_with_feedback()                            │
│       ↓                                              │
│  ┌─────────┐    ┌──────────────┐    ┌────────────┐ │
│  │  Tool   │───→│  Feedback    │───→│  Memory    │ │
│  │Registry │    │  Parser      │    │  Update    │ │
│  └─────────┘    └──────────────┘    └─────┬──────┘ │
│                                           │        │
└───────────────────────────────────────────┼────────┘
                                            ↓
┌─────────────────────────────────────────────────────┐
│                  Tiandao Core Layer                   │
│                                                      │
│  天道() ──→ tiandao_recall() ──→ 贝叶斯排序         │
│       │                                              │
│       └─→ tiandao_reason() ──→ 记录新经验           │
│                                                      │
│  experience_validate() ──→ 贝叶斯 trust 更新         │
│  experience_decay()   ──→ 周期性衰减                │
│  auto_promotion_check() ──→ 自动晋升/降级             │
│                                                      │
└─────────────────────────────────────────────────────┘
```

---

## 三、实施优先级

| 阶段 | 内容 | 影响范围 |
|------|------|---------|
| **P0** | 实现贝叶斯 trust 更新，替换硬编码的线性加减 | `experience.ku` L498-527 |
| **P0** | MCP 调用反馈环（代理层） | `mcp_server.py` |
| **P1** | 召回排序加权（recency × recall） | `tiandao.ku` L68-87 |
| **P1** | 自动晋升/降级调度 | 新增 `auto_lifecycle()` |
| **P2** | schema_version 字段 + 迁移脚本 | `memory.ku` L47 |
| **P2** | 贝叶斯衰减（α, β 乘 ρ） | `experience.ku` L600-613 |

---

## 四、结论

1. **Schema 已经就绪**——`confidence`, `trust`, `success_count`, `failure_count` 字段已存在，不需要改数据库结构，只需要改运行时逻辑。

2. **核心是打通反馈环**——MCP 调用→更新计数→贝叶斯 trust→影响下次排序，形成闭环。当前是开环，需要代理层介入。

3. **决策逻辑需要真实化**——`tiandao_reason` 必须从硬编码改为基于召回结果 + 贝叶斯推断的真实决策。

4. **当前 `experience.ku` 中已有贝叶斯雏形**（`experience_validate` 中的拉普拉斯平滑），但未被集成到调用路径，需要接线。

5. **跨模型版本持久化当前已满足**——记忆不依赖 embedding，只需显式加入 `schema_version` 防止未来 schema 漂移。

> 存疑汇总：贝叶斯衰减 vs 定期衰减的选择；success/failure 判定标准是否需要外部传入；embedding 辅助召回与纯文本召回的兼容策略。
