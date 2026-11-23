# R0：Record 编码与能力协商附录

> 日期：2026-11-22
> 状态：R0 协议设计；builder、VM 与只读 C API 已实现为 opt-in 内核切片，两套源码前端及汇编/反汇编已接入，ModuleStore、Python Runtime/CodeMemory 已支持显式能力策略，Python/Rust 已提供只读查询，未发布。范围见 [RECORDS.md](../kernel/RECORDS.md)。
> 上位设计：[Record / Schema / Result RFC](KU_RECORD_SCHEMA_RESULT_RFC.md)。本附录只定义 R1 标量 Record，不分配 Schema、Result/Option 或模块类型签名的编码。

## 1. 版本、能力与兼容矩阵

建议新增格式 **v3 / VM ABI11**，对应 `DAO_FEATURE_RECORDS = UINT64_C(1)`。
模块 header 的 magic、16 字节布局和 little-endian 编码沿用当前规则，flags 仍须为零。

| 模块 | 所需能力 | 旧内核（仅 ABI9/10） | 当前内核、默认 VM | 当前内核、启用 Record 的 VM |
|---|---:|---|---|---|
| v1/ABI9 | 0 | 接受合法模块 | 接受 | 接受 |
| v2/ABI10 | 0 | 接受合法模块 | 接受 | 接受 |
| v3/ABI11 | 1 | `DAO_BAD_MODULE` | `DAO_UNSUPPORTED_FEATURE` | 验证后接受 |
| 其他组合 | 无 | 拒绝 | `DAO_BAD_MODULE` | `DAO_BAD_MODULE` |

新状态 `DAO_UNSUPPORTED_FEATURE = 17` 追加到现有 `dao_status`，不移动已有 0–16 的编号；
`dao_status_name` 返回 `unsupported_feature`。未知格式不是能力协商，仍为 bad module。

所有新字段保持显式：不复用 `dao_vm_config.reserved`、`dao_host_function.reserved`、
`dao_value.reserved` 或旧指令的未用操作数。声明 Record 的模块使用 v3；
没有类型声明且不需要新指令的模块继续输出原来的 v1/v2，旧语料字节不变。
v3 至少含一个 Record 类型；不生成仅为预留功能的空类型模块。

v3 可无逻辑 identity，用于本地程序；若有 identity，其 METADATA 与 MODULE_IMPORT 两节同时存在。
独立模块仍按精确版本链接。R1 只允许既有安全值跨模块，Record 本身一律拒绝，不能凭 ABI11 就推断有类型签名。

## 2. Section 布局

section table 继续使用 16 字节 `(type, offset, size, count)` 四个 u32。
v3 必含 1–5、8、9 共七节；有 identity 时再含 6、7，共九节。其他节、重复节、非零保留字段拒绝。
1–7 的记录格式沿用 [FORMAT](../kernel/FORMAT.md)，没有重新解释既有字节。

| ID | 名称 | 记录大小 | 数量 |
|---:|---|---:|---|
| 8 | RECORD_TYPE | 16 | 1–1024 |
| 9 | RECORD_FIELD | 12 | 所有类型字段数之和，0–65536 |

RECORD_TYPE 记录：

| 偏移 | 类型 | 含义 |
|---:|---|---|
| 0 | u32 | `name_data`，类型名的 DATA 字符串索引 |
| 4 | u32 | `first_field`，RECORD_FIELD 中的起点 |
| 8 | u32 | `field_count`，0–256 |
| 12 | u32 | reserved，必须为 0 |

RECORD_FIELD 记录：

| 偏移 | 类型 | 含义 |
|---:|---|---|
| 0 | u32 | `name_data`，字段名的 DATA 字符串索引 |
| 4 | u32 | `value_type`，只能为 I64=1、TRIT=2、BYTES=3、STRING=4 |
| 8 | u32 | reserved，必须为 0 |

名称为 1–1024 字节的严格 UTF-8，不含 NUL。类型名在本模块按完整字节串唯一，字段名在所属类型内唯一；
不以 32 位 symbol hash 判断重名，也不做 Unicode 归一化或大小写折叠。源码前端另检查合法标识符。

字段区间必须按类型顺序连续覆盖整张字段表：首类型从 0 开始，下一个起点等于前一个起点加数量，
最终和等于字段表 count。空 Record 合法，其构造不读取参数寄存器。
`size == count * record_size`，计算先提升到 u64；再验证模块边界，不做截断后的比较。

确定性编码规则：

1. 类型按解析并完成源码组合后的声明顺序编号，字段按声明顺序编号。
2. 保留既有 DATA 字符串的顺序；按现有顺序追加 identity/模块依赖字符串，再依类型/字段顺序驻留新名称，使用精确字节去重。
3. section 按 ID 升序写入，起点按 8 字节对齐，间隙填 0；不追加尾部对齐。非空节不得重叠；空节可与下一节共用起点，不占用范围。
4. 导出仍按 symbol ID 排序，IMPORT 保持声明顺序，函数和代码仍保持编译次序。
5. 编码器只生成上述规范布局；加载器仍检查范围、大小和引用，不将物理排列不同但合法的输入冒充为编码器的规范产物。

## 3. 新指令与运行语义

保留 16 字节指令 `(opcode:u8, flags:u8, dst:u16, a:u16, b:u16, immediate:i64)`。
下列 43–45 仅在 v3/ABI11 有效；旧模块中出现时返回 `DAO_VERIFY_ERROR`。
每条指令消耗一个现有 VM 指令预算单位；最大字段数固定为 256，使构造/查找的单步工作量有界。

| Opcode | 名称 | 操作数 | 校验和执行 |
|---:|---|---|---|
| 43 | MAKE_RECORD | `dst` 结果；`a` 参数首寄存器；`b` 字段数；`immediate` 类型索引 | 静态检查类型索引和字段数量、`a+b <= register_count`；零字段时要求 a=0。执行时逐项严格检查值类型，按槽位复制并原子发布句柄 |
| 44 | RECORD_GET | `dst` 结果；`a` Record；`b` 字段槽位；`immediate` 预期本地类型索引 | 验证索引与槽位，执行时验证当前 VM 句柄、拥有模块和类型索引全部一致；返回字段值副本 |
| 45 | FIELD_GET | `dst` 结果；`a` 对象；b 必须为 0；`immediate` 为字段名 DATA 索引 | 名称必须满足第 2 节限制。Map 按既有缺失键返回空规则读取；同模块 Record 按完整字段名查找，不存在报类型错误；其他对象报类型错误 |

全部 flags 必须为 0，dst/a 的寄存器边界分别检查；MAKE_RECORD 的 a 在零字段情形只做规范值检查。
所有 immediate 必须非负且位于对应表内；`a+b` 采用提升后的整数计算。
dst 可以覆盖输入寄存器，但必须先完成全部输入检查和复制，再写 dst。
字段值是合法标签仍不够：Trit 范围、UTF-8 和借用视图边界继续按现有值规则验证。
动态字段类型不匹配返回 `DAO_TYPE_ERROR`；内存不足返回 `DAO_OUT_OF_MEMORY`，不留下可见的半成品 Record。

Record 类型身份为 `(拥有模块, 类型索引)`，不是结构形状。RECORD_GET 不接受另一模块的同名类型。
FIELD_GET 只负责动态点访问，不是跨模块类型转换。类型已知时前端应优先生成 RECORD_GET。
无类型信息的点访问在 v3 中使用 FIELD_GET；旧模块仍使用原有 INDEX_GET。
方括号访问仍使用 INDEX_GET，它拒绝 Record；INDEX_SET、LEN、MAP_KEYS 也不接受 Record。
Record 对 Record 的相等比较为类型错误；与不同类型比较遵循现有跨类型规则。

## 4. 值、归属与模块保活

建议 `DAO_VALUE_RECORD = 9`，`reserved = 0`，payload 为非零、不透明 64 位 ID。
这不改变 `dao_value` 的 16 字节布局，也不让模块字节包含运行时句柄。

现有容器实现使用 generation/index 编码，本提案不假定仅凭这两个局部编号就能证明 VM 归属。
新 Record ID 由进程级原子、单调且不复用的分配器产生；它只分配号码，不保存全局对象注册表。
每个 VM 仅在当前 generation 的本地表中将 ID 映射到 Record 实例。

- 零号无效；ID 的全部 64 位是位模式，不按 i64 正负解释，不向用户承诺数值或跨运行稳定性。
- 清理 generation 删除本地 ID 映射；其他 VM 的表不含该 ID，因此旧/外来句柄无法误命中。
- ID 空间耗尽不得回绕或复用，构造返回 `DAO_OUT_OF_MEMORY`。分配中留下未用 ID 不影响语言语义。
- 该方案不把句柄变成针对不可信 C 调用者的密码学凭证；它保证按 API 正常传递句柄时的归属和失效规则。

每个实例保存 `(owner module, type index, field values)`。VM 的本代 owner 集合对每个出现过的拥有模块 retain 一次；
清理时先销毁 Record/List/Map/Closure，再释放该集合。缓存清空、调用者 release、链接表变化不能提早释放描述。
借用的 Host/调用者 String/Bytes 仍由原 owner 保持有效；只保活模块不能延长任意外部缓冲区寿命。

`dao_vm_call` 先做普通参数和能力检查，再接受调用并清理上一代。顶层传入 Record 与 List/Map 一样，
返回 `DAO_INVALID_ARGUMENT`，不使上一代失效。调用已被接受后发生执行错误，则上一代已失效，与当前契约一致。

## 5. 新增 C API

以下接口已由当前内核头文件提供；两套源码前端可生成 Record 模块：

```c
#define DAO_FEATURE_RECORDS UINT64_C(1)

dao_status dao_vm_set_features(dao_vm* vm, uint64_t features);
dao_status dao_vm_get_features(const dao_vm* vm, uint64_t* out_features);
dao_status dao_module_required_features(const dao_module* module, uint64_t* out_features);

dao_status dao_module_record_count(const dao_module* module, uint32_t* out_count);
dao_status dao_module_record_name(const dao_module* module, uint32_t type_index,
                                  dao_bytes* out_name);
dao_status dao_module_record_field_count(const dao_module* module, uint32_t type_index,
                                         uint32_t* out_count);
dao_status dao_module_record_field_info(const dao_module* module, uint32_t type_index,
                                        uint32_t slot, dao_bytes* out_name,
                                        uint32_t* out_value_type);
dao_status dao_value_record_type(const dao_vm* vm, const dao_value* record,
                                  dao_module** out_owner, uint32_t* out_type_index);
dao_status dao_value_record_get(const dao_vm* vm, const dao_value* record,
                                 uint32_t slot, dao_value* out_value);
```

能力默认 0，R1 只接受掩码 0 或 1。unknown bits/空指针为 `DAO_INVALID_ARGUMENT`。
VM 在首次成功 load/link 或首次接受顶层 call 时锁定能力；此前允许改变掩码。
锁定后设置不同值返回 invalid argument，重复设置相同值幂等；失败不改变掩码、arena 或锁定状态。
VM 仍由外部同步，不允许运行中从 Host 回调修改能力。

类型查询对模块无副作用；旧模块 `record_count` 返回 0，其他类型索引查询按越界拒绝。
Record 查询要求启用能力；错误值标签/槽位/参数为 invalid argument，未启用为 unsupported feature，
旧或外来 Record ID 为 `DAO_RUNTIME_ERROR`。检查顺序为参数/标签、能力、句柄、槽位。
所有有效输出参数在失败时归零，传入 record 不变。

`dao_value_record_type` 成功返回 **已 retain** 的 owner，调用者须 `dao_module_release`。
名称视图借用该模块；释放最后一个 module 引用后不得继续使用视图。
record_get 只返回值副本，不给可变字段地址；其字符串/字节寿命按原 owner 规则，Record 句柄仍受 arena 限制。
本阶段不提供 `dao_vm_make_record`、字段 setter、retain Record 或跨调用再传入功能。

## 6. 能力检查、边界和状态码顺序

加载顺序：

1. 验证必要参数与模块大小，读取并检查 magic、版本对、flags 和 section table 的外层边界；坏输入为 bad module。
2. 从版本导出 required_features（v3 为 1），与目标 VM 比较；缺少能力返回 unsupported feature。
3. 再查缓存；命中也不能跳过能力检查。缓存键仍为指纹加完整字节比较，模块元数据记录版本和 required_features。
4. 检查全部描述、DATA、函数和指令。坏描述返回 bad module；坏指令操作数返回 verify error。
5. 成功后发布模块并锁定能力。失败时 out_module 为空，不插入部分缓存或 arena 内容。

模块对象可以被另一 VM 使用，因此 `link`、顶层 `call` 和 CALL_MODULE 目标进入时还要重新检查目标 VM 能力，
不能只信任创建模块的那个 VM。能力拒绝发生在调用被接受之前；不得清理旧 arena 或调用 Host。

R1 的边界表：

| 操作 | Record 本身或含 Record 的容器 |
|---|---|
| 同模块 CALL、同模块函数值/绑定、List/Map 存取 | 允许，同一代际；只读字段不变 |
| 顶层返回后由已启用的 C 调用者检查 | 允许 |
| 顶层参数 | 按 VM-owned 参数规则拒绝 |
| CALL_MODULE 的参数、结果和显式抛出载荷 | `DAO_TYPE_ERROR`，两端都是 ABI11 也不例外 |
| Host 回调参数或结果 | `DAO_TYPE_ERROR`，不调用未获准的 callback；检查不能只看顶层标签 |
| Host 容器插入 Record | `DAO_INVALID_ARGUMENT`，无容器修改 |

CALL_MODULE 结果/抛出载荷检查必须在控制权或异常值返回调用模块前完成；只检查 RETURN 会遗漏 THROW。
Host 参数先检查再调用；结果检查只能拒绝结果，不能承诺撤销 Host 已发生的副作用。
边界遍历覆盖 List/Map 的所有嵌套值，保留 Function/Closure 的既有禁止规则。
对图使用访问集合，重复句柄不重复展开，不能在循环容器上无限递归；模块路径继续服从既有深度限制。
启用 Record 的新边界检查对每个首次访问的值计一个共享预算单位，耗尽返回 `DAO_INSTRUCTION_LIMIT_EXCEEDED`，
Host 参数检查耗尽时 callback 尚未发生。只读 C 查询不消耗程序指令预算。

## 7. Builder、工具与绑定的实施要求

候选 C++ builder 增量接口：

```cpp
uint32_t add_record_type(std::string_view name);
uint32_t add_record_field(uint32_t type_index, std::string_view name, uint32_t value_type);
```

分别返回类型索引和类型内字段槽位；限制、唯一性和标量类型在添加时检查。
新类型存在则 encode 选 v3，并在编码前再次验证；无类型却出现新指令为错误。
自举 Host 对应 `builder_record_type/2`、`builder_record_field/4`，首参为 builder handle；
只在未 encode 且非 FunctionActive 状态添加，保留原 builder 状态机。新编号仅扩展 ABI11，不能扩大旧模块 opcode 上界。

| 路径 | R1 必须完成的事项 |
|---|---|
| 恢复/自举编译器 | 收集类型声明；按源码顺序求值并排布构造槽位；保留字段 AST；双前端与自重建门禁 |
| ModuleBuilder / loader / verifier | 全部新增表、版本选择、能力检查、数量与索引反例 |
| 汇编/反汇编 | 结构模型保存版本和类型字段表；文本表示及解析支持三个指令和完整描述，往返字节一致，不能丢弃类型表 |
| Register VM / C ABI | 新值验证、owner 保活、只读查询、边界递归、异常和缓存能力检查 |
| 数值 AOT | 在创建/截断输出文件前拒绝所有 v3 模块，保留现有数值模块路径 |
| ModuleStore | 增加显式 feature policy（构造参数 `features=0`），内部验证 VM 使用该策略；默认拒绝 v3，显式 opt-in 后可保存/读取模块；不修改 manifest schema-1 |
| Python / Rust | 暴露常量、能力和查询接口；默认不启用 Record；新值不能被当成 i64 或 Map。没有适配的调用入口明确报错 |
| dao-ku / SDK 测试入口 | 编译显式 Record 源码时，内部校验 VM 与测试 VM opt-in；不要让普通旧程序的 VM 默认打开能力 |
| dao-module / ku-config | 未适配的旧入口拒绝 v3；配置示例只有在 Record->Map 显式适配与 SDK 回归通过后才迁移 |

汇编文本语法已作为既有格式的 ABI11 扩展实现，见 [FORMAT.md](../kernel/FORMAT.md#abi11-assembly-text)。类型/字段/版本信息均保留；规范构建器输出的二进制可字节一致往返，不以省略类型表的文本作为往返产物。
R0 不加入空类型签名表、未来指令占位、Record 运行值序列化或自动数据迁移。

## 8. 可逐字节核对的样本

样本见 [scalar_record_v3.hex](fixtures/record_r0/scalar_record_v3.hex)，
[cases.json](fixtures/record_r0/cases.json) 记录在基准样本上修改的字节偏移、宽度、小端无符号值及预期状态。
这些验收数据已由 `record_wire_contract` 经生产 C ABI 验证；不用 Python 解析器代替生产 Verifier。
其余完整语言验收仍未完成。

样本语义为本地 `Point { x: 整数 }`，main 加载 42、构造 Point 并返回它；
导出 symbol(main)=3935363592，函数参数 0、寄存器 2，DATA 为 `Point` 和 `x`。

| Section | offset | size | count |
|---|---:|---:|---:|
| FUNC | 128 | 16 | 1 |
| CODE | 144 | 48 | 3 |
| EXPORT | 192 | 8 | 1 |
| IMPORT | 200 | 0 | 0 |
| DATA | 200 | 22 | 2 |
| RECORD_TYPE | 224 | 16 | 1 |
| RECORD_FIELD | 240 | 12 | 1 |

总长 252 字节，SHA-256 为 `959a982cba81c34b221c8ff699a2062b752b08b689b57369c3335d3319a7f2e3`。
CODE 为 `LOAD_I64(dst=0, imm=42)`、`MAKE_RECORD(dst=1, a=0, b=1, imm=0)`、`RETURN(a=1)`。
有效头为 `44 41 4f 00 03 00 0b 00 00 00 00 00 07 00 00 00`。

14 个样本含：合法构造、能力未启用、错配版本、header flags、未知 section、类型表截断、
类型名 DATA 越界、字段区间断裂、非标量字段、字段 reserved、构造类型越界、数量错误、寄存器越界及动态值类型不符。
动态类型样本把首条 LOAD_I64 改为 LOAD_TRIT(+1)：加载应通过，执行 MAKE_RECORD 时应返回 type error。

R1 还须新增多类型/重复名、空 Record、RECORD_GET/FIELD_GET、缺失字段、跨 VM、下一代、
module release/cache 清空、嵌套边界、THROW、循环容器、能力锁定、缓存命中和输出不截断测试。
单字段 wire 样本不能替代这些运行场景。

## 9. R0 完成判据与下一步

本附录已明确 R1 所需的版本对、能力 bit、状态码、值标签、section/记录尺寸、指令操作数、
C API、资源上限、句柄归属、生命周期、兼容矩阵与验收数据；没有以未来协议占位来宣称能力落地。

R1 内核格式/构造/字段读取与 C 查询已有可执行测试。下一步接入恢复和自举编译器、
汇编工具、ModuleStore 与绑定；生产入口开放时必须同时通过双前端和旧 ABI 回归。
R0 文档核对及旧加载器拒绝新版本的历史记录不作为执行证据；当前执行证据见 [RECORDS.md](../kernel/RECORDS.md)。
