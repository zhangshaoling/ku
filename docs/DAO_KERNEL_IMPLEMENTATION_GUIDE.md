# Dao 高性能机器语言内核实现指导

> 状态：唯一有效的内核实施指导
>
> 定位：供任何智能体、模型、程序和宿主系统嵌入的高性能机器语言运行时。
>
> 原则：不设计智能体，只实现足够快、足够小、足够稳定的语言与执行内核。

## 1. 产品边界

Dao 只负责：

- 机器可直接处理的程序表示。
- 快速加载、验证和执行。
- 稳定的跨语言调用 ABI。
- 确定性二进制模块和版本兼容。
- 可选的 AOT/JIT 本地机器码后端。

Dao 内核不定义 Agent、Thought、Memory、Trace、Patch、Tool、Goal 或 Experience。MCP、记忆、规划、自修复和模型 API 都是上层库或适配器。

```text
Existing Agent / Model / Application
  -> Dao C ABI
  -> verified Dao Binary Module
  -> Register VM or native backend
  -> result
```

## 2. 速度原则

设计优先级：

1. 执行速度。
2. 冷启动和模块加载速度。
3. 调用与 FFI 开销。
4. 内存和复制次数。
5. 确定性与安全验证。
6. 可移植性。
7. 文本书写体验。

生产执行不得解析文本、重新编译标准模块、经过 Python fallback 或通过 JSON 中转 FFI。

## 3. 执行架构

```text
text/json/typed builder
          |
          v
Dao Binary Module
          |
          v
loader + verifier
          |
          v
Register Bytecode
     |          |
     v          v
interpreter   AOT/JIT
                 |
                 v
           native machine code
```

文本、中文语法和 JSON 只用于构建、调试和迁移，不属于运行时热路径。

## 4. Dao Binary Module v1

```text
DAO\0                 magic
format_version        container version
vm_abi_version        bytecode ABI
flags
section_count
section_table

TYPE                  type declarations
SYMBOL                optional debug symbols
CONST                 constant pool
IMPORT                imports
EXPORT                numeric exports
FUNC                  function metadata
CODE                  register bytecode
DATA                  immutable binary data
DEBUG                 optional names/source maps
SIGNATURE             optional integrity data
```

相同模块必须得到相同二进制和相同哈希。portable module 不得包含内存地址。DEBUG section 可删除且不改变执行。

loader 必须检查：

- magic 与版本。
- section offset、length、count 和重叠。
- function code range。
- register index 与类型。
- branch target。
- import/export target。
- 最大模块、函数、寄存器和指令数量。

目标加载路径支持 mmap、只读 code/data 共享、按需 section 和 verifier cache。

## 5. 通用语言对象

内核对象只有：

```text
Module
Type
Constant
Function
Global
Import
Export
Value
Error
```

文本层可以把 Function 显示成“思”，但 ABI 只使用 numeric function id 和 numeric symbol id。

## 6. 值与类型

| 类型 | 说明 |
| --- | --- |
| `null` | 空值 |
| `trit` | `-1/0/+1` 三值逻辑 |
| `i32/i64` | 固定宽度整数 |
| `f32/f64` | IEEE 浮点 |
| `bytes` | 原始二进制 view/owned buffer |
| `string` | UTF-8 view/owned string |
| `array` | 连续同类型数据 |
| `list` | 动态有序值序列 |
| `map` | 高速映射，不承诺插入顺序 |
| `ordered_map` | 显式有序映射 |
| `struct` | 固定布局字段 |
| `function_ref` | numeric function handle |
| `external_ref` | 宿主资源 handle |

值表示必须通过 benchmark 比较 tagged union、NaN boxing、compact handle 和 arena，不能凭感觉确定。

首个稳定边界保持 `dao_value` 为 16 字节。`null/i64/trit` 使用零 reserved 字段；`bytes/string` 借用视图使用 32 位字节长度和 64 位指针，不复制数据，因此单视图上限为 4 GiB。`string` 进入 VM 或从宿主返回时必须通过严格 UTF-8 校验。VM 不拥有、不释放、也不延长视图内存生命周期；owned buffer 必须等 allocator/handle 契约确定后再加入。

## 7. Trit

```text
-1  negative / false
 0  unknown / neutral
+1  positive / true
```

```text
NOT(x)    = -x
AND(a,b)  = min(a,b)
OR(a,b)   = max(a,b)
```

`0`不得静默当作 false。字节码提供负、零、正三个独立分支。

标量使用一个 byte 或寄存器值。紧凑数据可使用两位编码或每字节打包 5 个 trit；是否打包由 benchmark 决定。

## 8. Register Bytecode ABI

新 ABI 使用寄存器式字节码。旧栈 VM 只作为迁移输入。

```text
LOAD_I64    r0, 40
LOAD_I64    r1, 2
ADD_I64     r2, r0, r1
RET         r2
```

每个 opcode 定义 numeric id、operand layout、输入输出类型、trap 和副作用类别。热 opcode 使用固定宽度布局，不用字符串分派。

首批 opcode：

```text
NOP
LOAD_I64
MOVE
ADD_I64
SUB_I64
MUL_I64
DIV_I64
TRIT_NOT
TRIT_AND
TRIT_OR
BR_TRIT_NEG
BR_TRIT_ZERO
BR_TRIT_POS
JUMP
CALL
RETURN
```

## 9. 执行与内存

Baseline interpreter 必须做到：

- function metadata 预解码。
- numeric opcode dispatch。
- 每条指令不分配 heap。
- 参数直接进入寄存器。
- immutable module 跨 VM instance 共享。
- VM instance 没有全局可变 registry。

热路径优先评估 frame arena、module arena、bump allocation、generational handle 和 pooled container。`shared_ptr`不能作为最终通用值模型。

## 10. Universal C ABI

```c
dao_vm* dao_vm_create(const dao_config* config);
dao_status dao_vm_load_module(dao_vm*, dao_bytes, dao_module** out);
dao_status dao_module_find_export(dao_module*, uint32_t symbol, dao_function* out);
dao_status dao_vm_call(dao_vm*, dao_module*, dao_function,
                       const dao_value* args, size_t argc,
                       dao_value* out);
dao_status dao_value_make_bytes_view(dao_bytes, dao_value* out);
dao_status dao_value_make_string_view(dao_bytes, dao_value* out);
dao_status dao_value_get_view(const dao_value*, dao_bytes* out);
void dao_module_release(dao_module*);
void dao_vm_destroy(dao_vm*);
```

C ABI 是最低公共边界。Python、Rust、Go、Node、Java 和 C# 只写薄绑定。支持 caller-owned buffer、zero-copy bytes/string view 和 numeric error code，不跨 ABI 抛 C++ exception。

## 11. FFI

外部能力通过 import table 与 host function registration 提供。内核不直接内置 SQLite、HTTP、filesystem policy、shell、environment、MCP 或模型 API。

FFI 避免 JSON 中转、重复 UTF-8 编解码和逐项容器复制。官方 host module 与第三方使用同一 ABI。

## 12. 构建入口

机器和智能体首选 typed builder：

```text
new_module
declare_type
add_constant
declare_function
emit_instruction
export_function
verify
encode
```

调用方不直接拼 offset。assembler/disassembler、文本与 JSON 只作为工具，并满足 canonical round-trip。

## 13. 性能门禁

第一稳定版：

| 指标 | 目标 |
| --- | ---: |
| VM create | < 2 ms |
| cached module load | < 2 ms |
| cold module load | < 10 ms |
| export lookup | < 500 ns |
| empty C ABI call | < 2 us |
| interpreter | > 10 M typed ops/s |
| core runtime memory | < 8 MB |
| kernel CI | < 3 min |

速度版目标：cached load < 500 us、empty call < 500 ns、interpreter > 50 M typed ops/s、AOT numeric loop 达到 native C 的 0.8x 以上。

所有数字由固定 benchmark suite 测量。当前 30 到 90 秒的 source/profile 调用必须从生产架构中移除。

## 14. 实施顺序

### K0：冻结契约

- Binary Module v1。
- Register Bytecode ABI v1。
- 核心值与 Trit。
- C ABI v1。
- benchmark suite v1。

### K1：Loader 与 Verifier

- deterministic encoder/decoder。
- strict bounds validation。
- module hash。
- fuzz harness。

### K2：Register VM

- typed registers。
- arithmetic、branch、call、return。
- baseline interpreter。
- compact value benchmark。

### K3：Module、Import、FFI

- numeric import/export。
- host registration。
- zero-copy buffer。
- Python/Rust 参考薄绑定。

### K4：Builder 与迁移工具

- typed builder。
- assembler/disassembler。
- legacy `.ku`迁移编译器。

### K5：AOT/JIT 与发行

- verified module cache。
- predecoded function cache。
- optional native backend。
- runtime library、headers、SDK、bindings 和工具。

### K6：上层生态

MCP、memory、agent、tiandao、life/self-repair 独立开发。它们是 Dao 的使用者，不是 Dao 内核。

## 15. 现有代码处置

现有 Python runtime、栈式 C++ VM、文本前端、`.ku`标准库和 Agent/Memory 模块都是迁移输入。

1. 不继续向旧 VM 增加大型功能。
2. 从旧 parity tests 提取语言行为。
3. 新建 Binary Module 与 Register VM。
4. 用迁移编译器逐步转换旧模块。
5. 新 C ABI 稳定后，上层自行迁移。

## 16. 完成定义

- 生产执行不解析文本、不经过 Python。
- Binary Module 可确定性编码、严格验证和快速加载。
- Register VM 使用 numeric typed opcode。
- C ABI 可被任意智能体和宿主调用。
- FFI 不依赖 JSON。
- 性能达到门禁。
- AOT/JIT 不改变语言 ABI。
- Agent、memory、MCP、life 的变化不要求修改内核。

Dao 的成功标准不是设计一个智能体，而是让任何智能体都能以更低成本获得更快的计算内核。
