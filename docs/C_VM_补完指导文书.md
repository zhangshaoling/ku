# C VM 补完指导文书

> 2026-07-08
> 给后续接手 `dao/dao_core.c`、`dao/compiler.py`、`dao/runtime.py`、`tests/test_c_vm_parity.py` 的模型。
> 目标：不要再按过期清单盲补，而是按当前真实缺口推进 C VM 完整度。

---

## 0. 先说结论

当前 C VM 的真实状态，比旧审计高很多。

不要再按下面这份旧判断继续干：

- `27/31 opcode`
- 缺 `SET_ATTR` / `IMPORT` / `BREAK` / `CONTEXT_COMPRESS`
- 大量基础 builtin 缺失

这些结论已经过时。按当前源码审计：

- `SET_ATTR` / `IMPORT` / `BREAK` / `CONTEXT_COMPRESS` 在 C VM 执行循环里都已经有分支
- `ceil/max/min/round/range/sleep/exit/is_int/bool/to_float/values/merge` 等基础 builtin 也已经在 C 里实现并注册
- 真正的缺口已经转移到：
  - Python/C builtin surface 不一致
  - 中文别名未对齐
  - `type()` 契约不一致
  - 若干高级 builtin 缺失
  - 一些 opcode 只有占位语义，没有完整行为
  - 对这些能力缺少 parity 门禁

一句话：**当前不是“从 0 补 runtime”，而是“做 ABI/语义对齐和剩余能力补完”。**

---

## 1. 必读文件

开始任何改动前，先读这些文件：

- `dao/dao_core.c`
- `dao/runtime.py`
- `dao/compiler.py`
- `dao/std/compiler.ku`
- `dao/std/type.ku`
- `tests/test_c_vm_parity.py`
- `docs/C_VM_接管审计.md`

核心关注点：

- C VM builtin 注册入口：`dao/dao_core.c`
- C VM builtin 分发表：`dao/dao_core.c`
- Python builtin 注入入口：`dao/runtime.py::_inject_builtins`
- Python VM opcode 语义：`dao/compiler.py::DaoVM`
- `.ku` 编译器实际发什么 bytecode：`dao/std/compiler.ku`

---

## 2. 当前真实基线

### 2.1 Opcode 基线

以下 opcode 在 C VM 中已经有执行分支：

- `SET_ATTR`
- `IMPORT`
- `BREAK`
- `CONTEXT_COMPRESS`

但注意：

- `SET_ATTR`：有基础行为，但前端源码链路未确认稳定产出
- `IMPORT`：当前更像 `load_ku` 桥接，不是完整模块系统
- `BREAK`：当前基本是占位，不是真正跳出循环
- `CONTEXT_COMPRESS`：纯占位

因此，正确说法不是“这 4 个 opcode 缺失”，而是：

- `SET_ATTR`：半完成
- `IMPORT`：桥接版完成
- `BREAK`：语义未完成
- `CONTEXT_COMPRESS`：未实现语义

### 2.2 Builtin 基线

Python runtime 有一套 builtin 注入表，C VM 也有一套注册表。

当前已经确认 **C 里已实现且已注册/分发** 的，不要重复补：

- 数学：`abs` `floor` `ceil` `max` `min` `round`
- 类型：`is_str` `is_list` `is_dict` `is_none` `is_int` `bool` `to_float`
- 容器：`len` `push` `has` `slice` `keys` `items` `values` `merge`
- 系统：`range` `sleep` `exit`
- 文件：`path_exists` `read_file` `write_file` `delete_file` `mkdir` `list_dir`
- 其他：`parse` `list_thoughts` `run_bytecode`
- 网络/HTTP/SQLite：已有较大面积实现

### 2.3 当前真正缺的英文 builtin

这批是 Python 有、C 没有的真实差集，优先关注：

- `save`
- `load`
- `str_format`
- `eval_ku`
- `create_thought`
- `clone_thought`
- `delete_thought`
- `get_thought`
- `thought_registry`
- `get_log`
- `clear_log`

### 2.4 当前真正缺的中文别名

Python 侧有一整套中文 alias，C 侧当前几乎只保留了：

- `且`
- `或`
- `非`

其余常用中文别名基本都还没补，至少包括：

- `示`
- `读文件`
- `写文件`
- `文_分割`
- `文_替换`
- `文_包含`
- `文_去空白`
- `文_是否为空`
- `文_小写`
- `文_大写`
- `文_起头`
- `文_结尾`
- `文_格式化`
- `文_转字符串`
- `列表长度`
- `类型`
- `范围`
- `整数`
- `浮点数`
- `布尔`
- `是字符串`
- `是列表`
- `是字典`
- `是整数`
- `是空`
- `字典键`
- `字典值`
- `合并`
- `有序`
- `字符`
- `追加`
- `含有`
- `切片`

### 2.5 类型系统真实问题

最危险的不是少几个 builtin，而是 `type()` 语义不一致。

当前：

- Python runtime 的 `type(x)` 更接近 Python 原生名字，如 `int` / `float` / `str`
- C VM 的 `type(x)` 返回的是 `num` / `str` / `bool` / `list` / `dict` / `fn`

这会直接导致 `dao/std/type.ku` 的判断在 C VM 下跑偏，因为它写的是：

- `type(x) == "int"`
- `type(x) == "float"`

所以 **类型契约对齐是第一优先级**。

### 2.6 错误处理真实问题

C VM 不是没有错误处理。

它已经有：

- `ExecResult`
- `frame_raise`
- `TRY_BEGIN` / `TRY_END` / `RAISE`

当前真实问题是：

- 一部分路径会抛错
- 一部分 builtin 参数错误只会返回 `0` / `false` / `nil`
- Python 侧很多地方则会直接抛异常

所以这里的任务不是“补异常系统”，而是“统一错误策略”。

---

## 3. 禁止误判项

后续模型必须避免以下误判：

### 误判 1

不要再说：

- “C VM 缺 `ceil/max/min/round/range/sleep/exit/is_int/bool/to_float/values/merge`”

这些已经在 `dao/dao_core.c` 里有实现。

### 误判 2

不要再说：

- “4 个 opcode 完全没有”

正确说法见本文件第 2.1 节。

### 误判 3

不要仅凭 `docs/C_VM_接管审计.md` 里的旧段落判断缺口。

这个文档内部有历史残留，必须以当前源码为准。

### 误判 4

不要先冲去补 `CONTEXT_COMPRESS`。

它当前没有明确的前端产出链路和测试需求，优先级最低。

### 误判 5

不要把“基础 builtin 不够”当成主矛盾。

当前主矛盾是：

- 契约不一致
- surface 不一致
- 门禁不够

---

## 4. 推荐执行顺序

建议拆成 6 个阶段。可以多模型并行，但必须按依赖关系收口。

### Phase 1：先修类型契约

目标：

- 对齐 Python/C 的 `type()` 行为
- 或让 `dao/std/type.ku` 不再依赖脆弱的类型名字字符串

推荐方案：

- 优先改 `dao/std/type.ku`，让它更多依赖：
  - `is_int`
  - `is_str`
  - `is_list`
  - `is_dict`
  - `is_none`
- 同时评估是否需要把 C 的 `type()` 也改得更接近 Python

原因：

- 改标准库比改底层 `ValType` 风险更小
- 能最快止住 C/Python 行为分叉

验收标准：

- `std/type.ku` 中 `is_int/is_float/to_int/to_float/is_num` 在 Python VM 和 C VM 下行为一致
- 新增 parity 测试覆盖这些场景

### Phase 2：补最容易收口的缺失 builtin

优先补这 5 个：

- `str_format`
- `save`
- `load`
- `get_log`
- `clear_log`

原因：

- 依赖关系相对清晰
- 不要求完整 Thought 注册表设计
- 可以较快建立“继续补高级 builtin”的模板

验收标准：

- Python/C builtin surface 对齐
- 新增最小 parity case

### Phase 3：补中文别名

目标：

- 把 Python `_inject_builtins` 里的中文 alias 表全量镜像到 C

最低要求：

- 常用 IO/string/type/list/dict alias 全部可用
- `文_转字符串` 单独补

验收标准：

- 中文别名至少覆盖 Python 侧现有表
- 新增少量中文 smoke/parity 测试

### Phase 4：补反射 builtin

这一组单独做，不要和 Phase 2 混一起：

- `eval_ku`
- `create_thought`
- `clone_thought`
- `delete_thought`
- `get_thought`
- `thought_registry`

注意：

- 这组在 Python 里依赖 `Thought.registry`
- C VM 没有完全同构的数据结构
- 先做“最小可用版本”，不要一开始就追求 Python 内部对象级完全等价

建议最小目标：

- `get_thought(name)` 返回可序列化摘要，而不是复杂内部结构
- `thought_registry()` 返回名字列表或摘要列表即可
- `create/clone/delete` 先围绕当前 Env 可见函数做最小实现

验收标准：

- 能被 `.ku` 标准库或测试程序调用
- 结果可序列化
- 不引入明显悬空指针/共享常量池问题

### Phase 5：统一错误策略

目标：

- 定义 builtin 参数错误、类型错误、运行错误到底怎样表现

至少要统一下面三类：

- 参数数量错误
- 参数类型错误
- 资源/系统调用错误

建议：

- 能进入 `try/catch` 的，尽量走 `frame_raise`
- 不能抛错的保底路径，也要有一致的返回约定

验收标准：

- 同类错误在 Python/C 下表现差异缩小
- `tests/test_c_vm_parity.py` 增加错误路径断言

### Phase 6：最后处理 opcode 语义尾巴

目标：

- 明确 `BREAK`
- 明确 `IMPORT`
- 明确 `CONTEXT_COMPRESS`

具体建议：

- `BREAK`：如果前端继续通过 jump patch 实现，可以保留为内部保底；否则补真正语义
- `IMPORT`：决定它是 runtime bridge 还是正式 bytecode 语义
- `CONTEXT_COMPRESS`：先文档化为保留指令即可，除非明确有前端需求

验收标准：

- 文档与实现一致
- 有最小测试，不再是“源码看着像支持，实际上没人验证”

---

## 5. 可并行拆分建议

如果是多模型协作，建议这样分工：

### 模型 A

负责：

- `dao/std/type.ku`
- `tests/test_c_vm_parity.py` 中的类型契约测试

### 模型 B

负责：

- `dao/dao_core.c` 的 `str_format/save/load/get_log/clear_log`
- 对应 parity 测试

### 模型 C

负责：

- 中文 alias 全量镜像
- 中文 smoke 测试

### 模型 D

负责：

- 反射 builtin 设计与最小实现
- 若需要，补充文档说明返回结构

收口顺序：

1. 先合 Phase 1
2. 再合 Phase 2 和 Phase 3
3. 最后合 Phase 4 到 Phase 6

---

## 6. 测试要求

后续任何补完，都不要只改实现不补门禁。

优先补到 `tests/test_c_vm_parity.py` 的能力：

- `type()` 契约对齐
- `str_format`
- `save/load`
- `get_log/clear_log`
- 中文 alias
- `eval_ku`
- `get_thought/thought_registry`
- `BREAK` 行为
- `IMPORT` 行为

测试策略：

- 优先最小 AST 或最小 `.ku` 程序
- 先做可稳定复现的 smoke case
- 不要上来就写超大集成测试

---

## 7. 修改约束

### 约束 1

不要回退用户已有改动，不要做大面积无关重构。

### 约束 2

如果只是补 surface 对齐，优先最小改动，不要重写 VM 主循环。

### 约束 3

涉及 `MAKE_FUNCTION`、closure、常量池、Env 链时要特别谨慎。

因为当前 C VM 已经支撑：

- frontend bootstrap
- `compiler.ku`
- `run_bytecode`

这些路径一旦被破坏，回归代价很大。

### 约束 4

凡是文档和源码冲突，以源码为准；修完后再回写文档。

---

## 8. 最小起手任务

如果只让一个模型先起第一刀，建议它做这件事：

### Task 1

修 `dao/std/type.ku` 的契约依赖，并补测试。

原因：

- 风险最低
- 收益最大
- 能立刻减少上层标准库在 C VM 下的隐性行为漂移

完成后再做：

### Task 2

补 `str_format/save/load/get_log/clear_log`

然后：

### Task 3

补中文 alias

---

## 9. 一句话执行摘要

后续模型不要再按“缺 4 个 opcode、缺一堆基础 builtin”的旧表施工。

正确路线是：

1. 先修 `type()` 契约
2. 再补缺失的 11 个英文 builtin
3. 再补中文 alias
4. 再统一错误策略
5. 最后清理 `BREAK/IMPORT/CONTEXT_COMPRESS` 这类尾部语义

这才是当前 C VM 的真实补完路径。
