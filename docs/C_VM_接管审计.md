# C VM 接管审计

> 日期：2026-06-13  
> 范围：`dao/dao_core.c`、`dao/dao_ipc.c`、`tests/test_c_vm_parity.py`

## 结论

`dao/dao_core.c` 是当前 C runtime 主线。它已经是 JSON bytecode VM 原型，可以执行 `dao/std/compiler.ku` 产出的部分 bytecode。

`dao/dao_ipc.c` 是旧的 S-expression IPC 实验，不应作为自举主线继续扩展。

## 本轮修复

`dao_core.c` 的 VM 指令分派存在一个阻断 bug：

```c
else if (strcmp(op, "JUMP") == 0) { ... }
if (strcmp(op, "JUMP_IF_FALSE") == 0) { ... }
else if (...) { ... }
else { unknown instruction }
```

`LOAD_CONST` 等指令执行后会继续落入后面的独立 `if/else` 链，最终报 `未知指令: LOAD_CONST`。

已修复为连续 `else if` 链，并移除两处 `[DBG]` 调试输出。

后续扩展 C/Python VM 对拍时又暴露 3 个 P0 行为差异：

- 主入口解析三元素指令时，把 `[op, arg, extra]` 误当成 `[op, null, real_arg]`，导致 `JUMP_IF_FALSE` false 分支跳转错误。
- `BUILD_LIST` 从栈弹出元素后直接追加，造成列表顺序反转。
- `dict_set` 头插导致字典输出顺序和 Python DaoVM 插入顺序不一致。

已修复为：

- 只有第二项为 `null` 时才按三元素指令格式取第三项，否则取第二项。
- `BUILD_LIST` 先按索引回填，再按原顺序追加。
- `dict_set` 新键使用尾插，保持插入顺序。

继续扩展字符串和内置函数对拍时暴露 3 个 P1 行为差异：

- 字符串拼接只保留字符串侧，`"n=" + 3` 会变成 `"n="`。
- `==`/`!=` 只按数字字段比较，两个不同字符串会误判相等。
- `type("abc")` 返回 `"string"`，和 Python DaoVM 的 `"str"` 不一致。

已修复为：

- 增加 `val_to_string`，字符串拼接和 `str()` 共用标量转字符串逻辑。
- 增加 `val_equal`，按类型处理 `nil/number/str/bool` 相等。
- `type()` 的字符串类型名对齐为 `"str"`。

本轮扩展控制流、函数和内置函数对拍，没有暴露新的 C 源码差异。新增覆盖：

- `has()` / `slice()`
- 赋值块和变量引用
- `SET_INDEX`
- `MAKE_FUNCTION` / `CALL` 用户 thought
- `while`
- `for`

加速轮继续扩展短路、递归、break/continue 和错误形状，暴露 1 个递归 thought 差异：

- Python DaoVM 在 `MAKE_FUNCTION` 时会按函数体内 `LOAD_CONST` 出现顺序重新映射常量索引。
- C VM 之前直接使用 body 指令里的常量索引，导致递归 `fib(6)` 算成 `32`，Python 参考结果为 `8`。

已修复为：

- C VM 的 `MAKE_FUNCTION` 对函数体 `LOAD_CONST` 使用和 Python DaoVM 一致的顺序归一化规则。

同轮新增 2 个未捕获错误门禁：

- 未定义名称必须非零退出并报告 `NameError`。
- 未捕获 `throw` 必须非零退出并报告 `RuntimeError`。

try/catch 轮接通了 C VM 的最小结构化异常：

- `Frame` 增加固定大小 `try_stack`。
- `TRY_BEGIN` 记录 catch 地址，`TRY_END` 弹出 handler。
- `RAISE` 在存在 handler 时压入错误字符串并跳到 catch 分支，否则保持非零退出。
- `GET_INDEX` 对 list 越界进入 catch，错误字符串对齐 Python DaoVM 的 `list index out of range`。

当前已覆盖：

- try 正常路径跳过 catch。
- throw 被 catch 捕获。
- list index 越界被 catch 捕获。

P1 edge 轮把 C VM parity 推到 50 个真实用例，新增覆盖：

- 一元 `not` / `-`。
- `>` / `>=` / `<=` / `!=`。
- 嵌套 thought 定义和调用。
- `LOAD_NAME` 未定义进入 catch。
- thought 内 `throw` 传播到调用者的 catch。
- 嵌套 try/catch 的错误再抛出。
- 未捕获 list index 越界错误形状。

本轮修复：

- `LOAD_NAME` 在有 try handler 时不再直接 `exit(1)`，而是压入 `ku-vm: '<name>' 未定义` 并跳到 catch。
- `ExecResult` 增加可传播错误字段，让函数调用内未捕获异常能交给调用者 frame 的 try handler。

边界语义轮把 C VM parity 推到 57 个真实用例，新增覆盖：

- dict 缺 key 返回 `null`。
- 字符串索引返回单字符。
- list 负索引按 Python/DaoVM 语义从尾部取值。
- list/dict `==` 使用结构相等。
- thought 闭包捕获外层 env。
- 未知 builtin/name 的错误形状。

本轮修复：

- `GET_INDEX` 支持 `V_STR`。
- `GET_INDEX` 对 list/string 支持负索引。
- `val_equal` 对 list/dict 改为递归结构比较。

builtin/numeric/index 轮把 C VM parity 推到 66 个真实用例，新增覆盖：

- `is_str` / `is_list` / `is_dict`。
- `*` / `/` / `%`。
- `SET_INDEX` 负索引 list 赋值。
- dict 已有 key 更新。
- 字符串索引越界错误形状。

本轮修复：

- `SET_INDEX` 对 list 支持负索引。
- `SET_INDEX` 对 list 越界和不可赋值对象进入异常路由。

container/string policy 轮把 C VM parity 推到 72 个真实用例，新增覆盖：

- `slice("abcd", 1, 3)` 字符串切片。
- `str([1, 2])` list/dict 递归字符串化。
- 字符串拼接容器，`string_concat_container` 路径通过。
- 嵌套 list/dict 结构相等。
- `SET_INDEX` 支持 dict 新 key 写入。
- `SET_INDEX` 写字符串按不可赋值对象报错。

本轮修复：

- `builtin_slice` 支持 `V_STR`。
- `val_to_string` 支持 list/dict 递归字符串化，`str()` 和字符串拼接共用该逻辑。

loop stack policy 轮把 C VM parity 推到 73 个真实用例，新增覆盖：

- `for` 循环体以 `SET_INDEX` 这类会返回栈值的表达式结尾时，下一轮迭代仍保持 iterator 在栈顶。

本轮修复：

- Python `DaoCompiler` 和 `compiler.ku` 在 `while` / `for` 循环体后丢弃表达式结果，避免循环体返回值污染循环协议。

keys/iteration policy 轮把 C VM parity 推到 75 个真实用例，新增覆盖：

- `keys(dict)` 按插入顺序返回 key 列表。
- `for k in keys(d)` 配合 `SET_INDEX` 可复制 dict 内容。

本轮修复：

- C VM 增加 `keys` builtin，并注册到全局环境。

memory-env C handoff 轮把 C VM parity 推到 82 个真实用例，新增覆盖：

- list + list 拼接。
- 递归 copy list/dict，覆盖 `type` / `keys` / `for` / `SET_INDEX` / 递归 thought 组合。
- 完整加载 `dao/std/memory_env.ku` 后，经 C VM 执行 `环境_新`。
- 完整加载 `dao/std/memory_env.ku` 后，经 C VM 执行 `环境_定义思` / `环境_记住` / `环境_注册工具`，结果与 Python DaoVM 一致。

本轮修复：

- `compiler.ku` 将 `LOOP_END` 识别为不产生栈值的指令，避免在循环后插入多余 `POP`。
- C VM 的 `POP` 对空栈安全，和 Python DaoVM 行为一致。
- C VM 的 `BINARY_OP +` 支持 list 拼接。

semantic std C handoff 轮把 C VM parity 推到 94 个真实用例，新增覆盖：

- 完整加载 `dao/std/memory_env.ku` 后，经 C VM 执行 `环境_忆起并记录` 成功和缺失路径。
- 完整加载 `dao/std/memory_env.ku` 后，经 C VM 执行 `环境_取轨迹` / `环境_取思` / `环境_取记忆`。
- 完整加载 `dao/std/trace.ku` 后，经 C VM 执行 `轨迹_调用影响` / `轨迹_记录成功` / `轨迹_错误影响`。
- 完整加载 `dao/std/patch.ku` 后，经 C VM 执行 `补丁_替换` / `补丁_转影响` / `补丁_应用` / `补丁_反向`。

本轮没有新增 C runtime 语义修复；上一轮的 `keys`、list 拼接、递归 thought、循环和 `SET_INDEX` 组合已经足够支撑这些入口。

semantic-core C handoff 轮把 C VM parity 推到 101 个真实用例，新增覆盖：

- `str_split("observe tests.fail", " ")`。
- 完整加载 `dao/std/semantic_core.ku` 后，经 C VM 执行 `语义_节点` / `语义_值转节点` / `语义_构造无参思`。
- 完整加载 `dao/std/patch.ku` 后，经 C VM 执行 add/remove/nested replace 的 `补丁_应用` 路径。

本轮修复：

- C VM 增加 `str_split` builtin，并注册到全局环境。

semantic combo demo 轮把 C VM parity 推到 102 个真实用例，新增覆盖：

- 同一个 bytecode 中完整加载 `semantic_core.ku` / `memory_env.ku` / `patch.ku` / `trace.ku`。
- 经 C VM 构造 `fix_bug` thought AST，注册进 Env，生成 Patch effect，并写入 Trace。

committed demo entry 轮把 C VM parity 推到 103 个真实用例，新增覆盖：

- 新增 `demos/semantic_std_combo.kub.json`，作为可由 C VM 直接读取执行的固定 bytecode demo。
- 新增 `tools/generate_semantic_std_combo_demo.py`，用于在语义标准库或 `compiler.ku` 变化后重新生成 demo bytecode。
- C VM 通过 argv 文件路径执行 committed demo，不经过 pytest 动态拼接 bytecode。

compiler handoff 轮把 C VM parity 推到 104 个真实用例，新增覆盖：

- 完整加载 `dao/std/compiler.ku` 后，经 C VM 执行 `compile_ast` 编译 `1 + 2` AST。
- C VM 输出的 bytecode 与 Python DaoVM 输出一致，并且该 bytecode 可由 Python DaoVM 执行得到 `3`。

本轮没有新增 C runtime 语义修复；前序的 list/dict/string、循环、函数调用、索引赋值和容器字符串化能力已足够支撑 `compiler.ku` 的最小入口。

frontend compile demo 轮把 C VM parity 推到 105 个真实用例，新增覆盖：

- 新增 `demos/frontend_compile_demo.kub.json`，作为可由 C VM 直接读取执行的固定前端自举 bytecode demo。
- 新增 `tools/generate_frontend_compile_demo.py`，用于重新生成该前端 demo bytecode。
- C VM 通过 argv 文件路径执行 `frontend_compile_demo.kub.json`，在 VM 内运行 `lexer.ku` / `parser.ku` / `compiler.ku`，对小型单行程序完成 `lex -> parse_tokens -> compile_ast` 并返回生成的 bytecode。

frontend literal expansion 轮把 C VM parity 推到 108 个真实用例，新增覆盖：

- `str_contains` / `int` / `float` builtin。
- C VM JSON 字符串解析支持常见转义：`\n` / `\t` / `\r` / `\b` / `\f` / `\"` / `\\` / `\/`。

## C builtins 缺口审计

2026-06-13 的 std AST 扫描发现，C VM 已补齐自举前端和常用标准库所需的大部分 runtime builtins。本轮新增：

- 字符串：`str_starts_with` / `str_ends_with` / `str_upper` / `str_lower`
- 字典和值：`items` / `is_none`
- 文件目录：`path_exists` / `read_file` / `write_file` / `delete_file` / `mkdir` / `list_dir`
- 系统桥接：`now` / `system`

随后补齐反射入口：

- `parse(code)`：在 C VM 中调用当前 bootstrap 环境里的 `lex -> parse_tokens`，返回单节点优先 AST。
- `list_thoughts()`：枚举当前 Env 链中的可见名字。
- `run_bytecode(bytecode)` 改为继承当前 Env，而不是总是创建全新 global；这样用户 bytecode 可以继续访问 bootstrap 前端函数。

补齐后，`dao/std/*.ku` 里剩余未落到 C builtin 或本地 thought 定义的调用集中为：

- `sqlite_open` / `sqlite_exec` / `sqlite_query` / `sqlite_close`
- `并行执行`
- 审计误报：函数参数调用 `fn(...)`，以及导入别名 wrapper `文_连接`

下一轮优先级：

1. SQLite：决定是否引入 `-lsqlite3` 链接依赖，或先以标准库可降级策略替代。
2. 并行执行：先定义 C VM 调度语义，再实现。
- `frontend_compile_demo.kub.json` 已扩展为包含换行、数字字面量和字符串字面量的小程序：`x = 1; id("ok")`。

frontend run-bytecode 轮把 C VM parity 推到 109 个真实用例，新增覆盖：

- C VM 增加 `run_bytecode` builtin，可在 VM 内执行由 `compile_ast` 返回的 bytecode dict。
- Python bootstrap runtime 增加同名 `run_bytecode` builtin，确保 `.ku` 侧调用在 Python DaoVM 和 C VM 中可对拍。
- `frontend_compile_demo.kub.json` 改为在 VM 内完成 `lex -> parse_tokens -> compile_ast -> run_bytecode`，直接返回小程序结果 `ok`。

frontend std-shape demo 轮保持 C VM parity 为 109 个真实用例，扩展既有 committed demo 覆盖：

- `frontend_compile_demo.kub.json` 的源码片段升级为 lexer 风格的 `make_token(type, value, line, col)` thought。
- C VM 在同一进程内经 `.ku` frontend 编译并执行该片段，返回 token dict：`{"type":"name","value":"x","line":1,"col":1}`。
- 该路径覆盖 thought 参数、dict literal、字符串 literal、数字 literal、转义换行、`compile_ast` 和 `run_bytecode` 组合。

frontend semantic-node demo 轮保持 C VM parity 为 109 个真实用例，继续扩展既有 committed demo 覆盖：

- `frontend_compile_demo.kub.json` 的源码片段升级为 semantic-core 风格的 `语义_节点(类型, 值, 子节点)` thought。
- C VM 在同一进程内经 `.ku` frontend 编译并执行该片段，返回 canonical node dict：`{"type":"literal","value":"x","children":[]}`。
- 该路径覆盖中文 thought/参数名、dict 新 key 写入、字符串索引赋值语法、list literal、字符串 literal、转义换行、`compile_ast` 和 `run_bytecode` 组合。

frontend bootstrap CLI 轮把 C VM parity 推到 110 个真实用例，新增源码入口覆盖：

- 新增 `demos/frontend_bootstrap.kub.json`，只加载 `lexer.ku` / `parser.ku` / `compiler.ku` 定义，不绑定固定源码。
- 新增 `tools/generate_frontend_bootstrap.py`，用于在前端标准库或 compiler 变化后重新生成 bootstrap image。
- `dao_core` 新增 `--bootstrap <frontend_bootstrap.kub.json> <program.ku>` 模式，在 C 侧读取 `.ku` 源码并执行 `lex -> parse_tokens -> compile_ast -> run_bytecode`。
- C VM 的函数对象保存所属 bytecode 常量池，修复 bootstrap 函数跨 frame 调用时使用调用者常量池导致的段错误。
- 新增门禁验证 C CLI 可编译并运行外部 `.ku` 源文件：`思 加一(数) { 数 + 1 } 加一(41)` 输出 `42`。

frontend multi-source std CLI 轮把 C VM parity 推到 111 个真实用例，继续扩展源码入口：

- `dao_core --bootstrap` 支持多个 `.ku` 源码参数，按顺序拼接后交给 `.ku` frontend 编译。
- 新增门禁验证 C CLI 可加载真实 `dao/std/semantic_core.ku`，再加载用户程序调用 `语义_构造无参思(...)`。
- 该路径证明 C 侧源码入口已能组合标准库源码和用户源码，不再只依赖单文件小程序。

frontend std-source hardening 轮把 C VM parity 推到 117 个真实用例，新增覆盖和修复：

- `.ku` parser 支持 `否 若 ... { ... }`，修复 `math.ku` 只解析到 `clamp` 后吞掉后续源码的问题。
- `.ku` parser 和 Python parser 支持 `捕(e) { ... }`，修复 `string.ku` 只解析到 `chr` 后吞掉后续源码的问题。
- C VM 的字符串 `len` / 索引 / `slice` / `ord` / `chr` 改为 UTF-8 字符语义，不再按字节切中文。
- 新增门禁验证 C CLI 可加载真实 `math.ku` 并执行 `求和([1,2,3])` / `斐波那契(10)`。
- 新增门禁验证 C CLI 可加载真实 `string.ku` 并执行中文 `连接` / `反转文本` / `取字符`。
- 当前 `dao/test_core.ku` 仍阻塞在模块导入语法：`引 "std/math" 别 数` 会在 C 源码入口报 `NameError: '别' 未定义`。

frontend bootstrap regeneration 轮把 C VM parity 推到 133 个真实用例，新增覆盖和修复：

- 新增门禁验证 C VM 通过 `--bootstrap frontend_bootstrap.kub.json dao/std/bootstrap.ku regenerate.ku` 在 VM 内运行 `写前端自举字节码(...)`，重新生成 frontend bootstrap image。
- 再用该 regenerated image 执行外部 `.ku` smoke program：`思 加一(数) { 数 + 1 } 加一(41)`，验证再生 image 的函数参数绑定、中文参数名、`lex -> parse_tokens -> compile_ast -> run_bytecode` 闭环可用。
- 修复 `.ku` lexer 的标点识别：将深层嵌套 `或(...)` 链改为显式布尔赋值，避免 C VM 再生 image 中 `)` 丢失，导致 `f(1)` 被解析为无参调用。
- 修复 `.ku` parser 中 `_call_args` / `_list` / `_dict` 的顶层分隔判断：将 `且(a, b)` 形式拆为显式布尔变量，避免再生 bootstrap 的函数调用参数、列表项和字典 pair 分割退化。
- C VM 的 `MAKE_FUNCTION` 对函数体 `LOAD_CONST` 归一化改为优先保留已在常量池范围内的绝对索引，仅在越界时回退到 `const_offset + load_idx`，兼容 Python harness image 和 C VM regenerated image 的常量索引形态。

## 已建立门禁

新增 `tests/test_c_vm_parity.py`：

- 使用 `compiler.ku` 生成真实 bytecode。
- 用 Python `DaoVM` 执行得到参考结果。
- 通过 WSL 编译并运行 `dao_core.c`。
- 比较 C VM 输出和 Python DaoVM 输出。

当前覆盖：

```text
literal_number
binary_add
binary_sub
equality_number
comparison_lt
if_true
if_false
list_literal
dict_literal
index_lookup_list
index_lookup_dict
string_literal
string_concat
string_concat_number
list_concat
builtin_str_split
builtin_str_contains_true
builtin_int_string
builtin_float_string
builtin_run_bytecode
equality_string_true
equality_string_false
builtin_len_list
builtin_type_string
builtin_has_true
builtin_slice_list
nested_list_dict
block_assign_ref
index_assign_list
thought_call
while_loop
for_loop
for_body_index_assign_discards_value
for_keys_dict_copy
recursive_copy_list
recursive_copy_nested_dict
if_without_else_false
and_short_circuit_false
or_short_circuit_true
builtin_ord_chr
builtin_push_mutation
while_break
while_continue
recursive_thought
missing_name_error
throw_uncaught
try_success
try_catch_throw
try_catch_index_error
unary_not
unary_minus
comparison_gt
comparison_ge
comparison_le
comparison_ne
nested_thought_call
try_catch_name_error
try_catch_thought_throw
nested_try_catch
list_index_out_of_range_error
dict_missing_key_returns_null
string_index
negative_list_index
list_equality_true
dict_equality_true
closure_capture
call_unknown_builtin_error
builtin_is_str_true
builtin_is_list_true
builtin_is_dict_true
builtin_keys_dict
numeric_mul
numeric_div
numeric_mod
builtin_slice_string
str_list
string_concat_list
nested_container_equality
set_index_negative_list
set_index_dict_new_key
dict_update_existing_key
string_index_out_of_range_error
set_index_string_error
memory_env_new
memory_env_define_thought
memory_env_remember
memory_env_register_tool
memory_env_recall_existing
memory_env_recall_missing
memory_env_get_trace
memory_env_get_thought
memory_env_get_memory
trace_call_effect
trace_record_success
trace_error_effect
semantic_node
semantic_value_to_node_dict
semantic_construct_short_thought
patch_replace
patch_to_effect
patch_apply_replace
patch_inverse
patch_apply_add
patch_apply_remove
patch_apply_nested_replace
semantic_std_combo_demo
committed_semantic_std_combo_demo_file
compiler_compile_binary_add
committed_frontend_compile_demo_file
```

普通无 WSL 权限环境下测试会 skip；有 WSL 权限时执行真实对拍。

最近一次真实 WSL 对拍结果：

```text
109 passed in 44.59s
```

## 当前可接管范围

可以认为 C VM 当前具备最小 P0 bytecode 执行能力，并开始接管部分 `.ku` 语义标准库入口：

```text
LOAD_CONST
BINARY_OP + / list+list / - / == / != / <
container structural equality
JUMP_IF_FALSE
JUMP_IF_TRUE
JUMP
DUP / POP
BUILD_LIST
BUILD_DICT
GET_INDEX
GET_INDEX string / negative list index
CALL len / str(list/dict recursive) / str_split / str_contains / int / float / type / ord / chr / push
CALL run_bytecode
CALL has / keys / slice(list/string) / user thought
MAKE_FUNCTION
STORE_NAME / STORE_FAST / LOAD_NAME
SET_INDEX
SET_INDEX negative list index / dict new key / string assignment error
LOOP_BEGIN / LOOP_END
GET_ITER / FOR_ITER
TRY_BEGIN / TRY_END
RAISE uncaught / caught
RETURN
```

这还不是 runtime 接管，只是 bytecode VM 最小可验证入口。

## 主要风险

1. 错误处理仍然过硬  
   多处 `exit(1)`，不适合长期 runtime。应改成结构化错误返回。

2. 内存所有权未定义  
   `malloc/strdup/realloc` 大量存在，释放策略不完整。短进程可跑，长期服务不可接受。

3. Value 语义未完全对齐 Python/Dao  
   已知风险：
   - `==` 已覆盖数字、字符串、布尔、空值、list、dict；function 仍按指针身份比较。
   - 字符串拼接已覆盖标量和 list/dict 递归字符串化。
   - list/dict 已覆盖简单 literal、嵌套 literal、index、负索引、结构相等和 SET_INDEX。

4. JSON parser 极简  
   已覆盖常见字符串转义，但 Unicode `\uXXXX`、错误输入和边界场景仍不稳。

5. WSL 是当前运行依赖  
   Windows Python 不能直接运行 ELF `dao/dao_core`。当前测试通过 WSL 执行。

## 下一步

不要立刻大改 C。应按对拍测试暴露的问题逐个补：

1. 继续扩展 `tests/test_c_vm_parity.py` 到：
   ```text
   import_opcode_policy
   ```
2. 异常接管继续成组推进，下一轮重点是边界错误和嵌套异常：
   ```text
   try_catch_call_unknown_builtin
   try_catch_string_index_out_of_range
   try_catch_set_index_error
   try_finally_shape
   ```
3. 哪个失败修哪个 opcode、异常路由或 Value 语义。
4. 修完 P0/P1 bytecode 等价后，立即做内存所有权模型。
5. 等 C VM 对拍稳定后，再考虑替换 Python DaoVM 的执行路径。
