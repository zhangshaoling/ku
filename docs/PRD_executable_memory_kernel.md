# 可执行记忆核 PRD — `dao_tiandao` v2

> 目标：把"半吊子"的天道决策循环替换为可执行的记忆生命周期引擎，使 trust/confidence 成为可观测、可晋升、可衰减的真实变量。

---

## 1. 什么算"记忆"？

| 类型 | 表 | 定义 | 关键字段 |
|------|-----|------|-----------|
| experience | experience | 单次运行产生的记录（尝试/观察/缺口/数据记忆） | kind, topic, input, output, success/failure_count |
| knowledge | knowledge | 从外部或推断产出的领域陈述 | domain, content, source |
| concept | concept | 抽象定义 + 示例 + 领域 | name, definition, examples_json |
| goal | goal | 当前任务目标，含进度和优先级 | name, priority, status, progress |

**并存**：experience 和 knowledge 的区别仅在于"来源是否可追溯到一次运行"。一个 knowledge 可以由 promotion 从 experience 析出（标记 `tool_generated=1`）。

---

## 2. 什么算"工具"？

**唯一权威**：`tool_registry` 表。

一条 experience 经过 `工具晋升条件`（见 §5）后由 `tool_promote()` 写入 `tool_registry`，成为可调用的"道器"。每个 tool 独立追踪 `call_count / success_count / fail_count / trust / confidence`。

**注意**：目前 `tiandao_reason()` 是占位符，未参与晋升判定——下一个版本必须让"道计算结果"成为晋升的输入。

---

## 3. 什么时候"记"？

| 时机 | 触发 | 过滤规则 |
|------|------|----------|
| 推理完成 | `tiandao_reason()` 返回 ok | 必须非空 output；重复 topic+context 合并而非追加 |
| 外部知识注入 | knowledge_add() | 同一 (domain, content) 去重；source 字段必填 |
| 任务完成校验 | experience_validate(id, success) | 只对 `tool_generated=1` 或 `kind=attempt` 的经验生效 |
| 召回命中 | memory_recall / experience_recall_one | 先写后读：recall_count++ 发生在返回前 |

**过滤红线**：`confidence < 0.1` 且 `success_count = 0` 的经验不写入（冷启动保护）。

---

## 4. 什么时候"忘"？

**衰减**（每日或每次会话启动调用一次）：

```
trust = trust * 0.99^(days_since_last_recall)
confidence 不变（由 success/failure 驱动）
```

| 条件 | 动作 |
|------|------|
| `trust < 0.1` 且 30 天未召回 | quality_status = 'decaying'（不再参与晋升和默认召回） |
| `quality_status = 'decaying'` 再经 90 天 | quality_status = 'archived'；保留记录但 FTS 索引移除 |
| `confidence < 0.15` 且总调用 > 10 | 强制归档（证据充分的淘汰） |

---

## 5. 什么时候"晋升"？

经验 → tool 的充要条件（同时满足）：

| 指标 | 阈值 | 说明 |
|------|------|------|
| trust | ≥ 0.75 | 由 success 累加、failure 折损 |
| recall_count | ≥ 3 | 被召回使用过至少 3 次 |
| 成功率 | ≥ 60% | success_count / (success_count + failure_count) |
| confidence | ≥ 0.6 | 贝叶斯收缩后验（小样本向 0.5 回归） |

**双向检查**：每次 `tool_promote()` 前先用 `规则_晋升检查()` + `规则_衰减检查()` 各跑一次。

---

## 6. 什么时候"降级"？

| 条件 | 动作 |
|------|------|
| 连续失败 ≥ 3 次 | tool status='demoted', quality_status='decaying' |
| 90 天未召回且 trust < 0.3 | 同上 |
| 成功率 < 30% 且总调用 > 5 | 同上 |

降级不是删除——`tool_demote()` 只改状态，原 experience 记录保留可供审计。

---

## 7. 跨模型版本持久化

**问题**：模型升级后，由旧模型推断的 confidence 可能系统性偏移。

**规则**：

| 字段 | 用途 |
|------|------|
| `model_version` | 推断该条记录时使用的模型版本（新增列） |
| `content_trust` | 纯内容维度的信任（不随模型变化） |
| `model_calibration` | 该版本模型的整体校准系数（版本表维护） |

**最终置信度**：`confidence = content_trust * model_calibration`

模型升级时：旧记录的 content_trust 不变；model_calibration 由新旧模型在基准用例上的表现比值更新。

**存疑**：基准用例集的维护频率未确定——建议每月一次。

---

## 8. 跨会话 / 跨数据流 / 跨网关 来源置信度

**来源加权表**（`source_weight` 影响初始 confidence）：

| 来源 | 初始 confidence | 说明 |
|------|----------------|------|
| 直接执行结果（tool/attempt） | 0.7 | 有可观测的 success/failure |
| 推理产出（tiandao_reason） | 0.5 | 尚未被证伪，但无直接验证 |
| 外部注入（knowledge_add） | 0.4 | 未经验证的领域陈述 |
| 跨网关转发 | 0.3 | 来源不可审计 |

**数据流隔离**：每个 experience 记录 `data_stream_id`（会话 + 网关组合标识）。跨 stream 的 recall 按 `source_weight` 加权排序，优先取本 stream 的高置信邻居。

**跨网关**：如果同一 topic 在多个网关有记录，合并显示时按 source_weight 降序，但保留各自 source 标签——不允许把网关 A 的 confidence 迁移给网关 B。

---

## 总结：v2 必须替换的占位符

| 当前（占位） | v2（可执行） |
|-------------|--------------|
| `tiandao_reason` 返回固定字符串 | 改为真实调用体验证函数，输出 confidence 依据 experience_quality |
| trust/confidence 从不衰减 | 每次会话启动调用 `experience_decay()` |
| recall 纯 SQL 排序 | 增加 source_weight 加权 + 召回前 counter 递增 |
| 无 model_version 字段 | experience 表新增 `model_version` + `data_stream_id` |
| 晋升/降级检查独立调用 | 改为 `每次 tool_call 前后各一次` |
