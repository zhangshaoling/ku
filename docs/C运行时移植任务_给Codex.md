# Codex 任务：道语言 C 运行时

> 2026-06-04 | 天书交接
> 目标：将 Python 运行时（runtime.py）的核心原语移植到 C

---

## 背景

道语言（ku）是一套认知 DSL，已有 442 个 .ku thought 作为认知层代码。
当前这些 thought 由 Python 运行时（runtime.py）解释执行。

道语言的架构：

```
认知层（442个 .ku thought）     ← 已有，不改
    ↕ 原语 ABI 契约
运行时实现（C / Python / …）    ← 你现在要写 C 版
    ↓
OS
```

关键文件位置：
- 道语言原语规范：`D:\Tools\Dao\docs\道语言原语规范.md`（ABI 契约，必读）
- Python 参考实现：`D:\Tools\Dao\dao\runtime.py`
- 现有 C 原型：`D:\Tools\Dao\dao\dao_core.c`（共享库版）
- 现有 C IPC：`D:\Tools\Dao\dao\dao_ipc.c`（管道通信版）

验证内容：
- 认知层核心：`D:\Tools\CppLearning\天书读道德经.ku`
- 中文别名测试：`D:\Tools\Dao\test_fix_verify.py`

---

## 任务：实现 C 运行时

### 阶段 1 — 修复现有 C 代码的内存问题（半天）

当前 `dao_core.c` 和 `dao_ipc.c` 最大的问题是**双释放 / 野指针 / 内存泄漏**。
原因：`lval_del()` / `val_del()` 在多个位置释放同一指针。

**具体要求：**

```
1. 设计所有权模型
   - 每个 lval 只能有一个"所有者"
   - lenv_get() 应返回副本，不是原始指针
   - lval_eval() 对原始类型应返回副本（已部分修复）

2. 移除 hack
   - 当前 val_del() 被置为"进程退出时 OS 回收"的空函数
   - 需要实现正当的递归释放

3. 实现的释放函数
   - lval_del() — 递归释放 lval 及其子节点
   - lenv_del() — 释放环境及所有符号-值对
   - 无循环引用，简单引用计数就够
```

**参考：**
- 现有 dao_core.c 第 94-108 行（旧版 lval_del，之前被注释掉）
- 现有 dao_ipc.c 第 43-46 行（当前空的 val_del）

### 阶段 2 — 实现 P0 计算内核（1天）

根据 `道语言原语规范.md`，P0 包含 7 个原语：

```
+, -, *, /, %, ==, <
```

这些已在部分代码中实现（builtin_add 等），但需验证：
- 浮点数支持
- 除零返回 Error 而非崩溃
- == 支持字符串、符号比较

**编译方式：**
```bash
# WSL kali-linux
wsl -d kali-linux -- gcc -shared -fPIC -o dao_core.so dao_core.c -lm -O2

# Windows (若需 MinGW)
gcc -shared -o dao_core.dll dao_core.c -lm -O2
```

### 阶段 3 — 设置 Python C 扩展（1天）

将 C 运行时编译为 Python 可直接调用的 C 扩展。

**方案 A — ctypes（推荐短期）：**
```c
// 导出的 API：
void*  dao_env_new();
char*  dao_eval(void* env, const char* code);
void   dao_set(void* env, const char* name, const char* value);
char*  dao_get(void* env, const char* name);
void   dao_free_str(char* str);
void   dao_env_free(void* env);
```

Python 端（参考 `D:\Tools\Dao\dao\dao_core_c.py`）：
```python
import ctypes
lib = ctypes.CDLL("./dao_core.so")
env = lib.dao_env_new()
result = lib.dao_eval(env, b"(+ 1 2)")
```

**方案 B — Python C Extension（推荐长期）：**
```c
// 标准 Python C 扩展接口
static PyObject* dao_eval_py(PyObject* self, PyObject* args) {
    // 接收 Python 字符串 → C 字符串 → eval → 返回 Python 结果
}
```

**关键约束：**
- C 代码在 WSL 编译，Windows Python 加载不了 .so
- 方案：在 WSL 里跑 Python，或在 Windows 上装 MinGW 编译 .dll
- 目前道语言运行时在 Windows Python 3.12.9，PYTHONPATH=D:\Tools\Dao

### 阶段 4 — 实现 P1 原语（1天）

P1 包含 8 个核心认知原语：

```
示, 读文件, 写文件, 文_分割, 文_包含, 文_替换,
文_去空白, 文_是否为空
```

**实现要点：**
- `示` → printf 输出，返回输入值（与 Python 一致）
- `读文件` → fopen + fread，UTF-8 编码
- `写文件` → fopen + fwrite，覆盖模式，自动创建目录
- `文_分割` → strtok / 手动分割，返回字符串列表
- `文_包含` → strstr (C) 或 KMP（中文场景）
- `文_是否为空` → 检查 NULL/空字符串/全空白

**验证方式：**
```c
// 用 dao_eval 直接测试
printf("%s\n", dao_eval(env, "(示 \"hello\")"));
printf("%s\n", dao_eval(env, "(读文件 \"test.txt\")"));
printf("%s\n", dao_eval(env, "(文_分割 \"a,b,c\" \",\")"));
```

### 阶段 5 — 集成验证（半天）

将 C 运行时接入道语言的认知层代码。

**测试流程：**
```python
# Python 端
from dao.dao_core_c import CEvalEngine
engine = CEvalEngine()

# 测试原语等价性
assert engine.eval("(+ 1 2)") == "3"
assert engine.eval("(文_是否为空 \"\")") == "True"
assert engine.eval("(文_分割 \"a,b\" \",\")") == '["a","b"]'

# 运行认知层
engine.load_ku("agent_core.ku")     # 逐步加载 .ku 文件
engine.run("__decompose", "refactor project X")
```

---

## 已知问题

### Bug 1：realloc 缩容到 0 导致未定义行为
```c
// dao_ipc.c | dao_core.c
lval* lval_pop(lval* v, int i) {
    // ...
    v->count--;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);  // ← 当 count=0 时 UB
    return x;
}
```
**修复：** 不缩容，仅在扩容时 realloc。或 count=0 时设置 cell=NULL。

### Bug 2：lval_builtin 未初始化字段
```c
lval* lval_builtin(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN; v->builtin = func;
    // ❌ env/formals/body 未初始化
}
```
**修复：** 用 calloc 或显式初始化所有字段。

### Bug 3：WSL ↔ Windows 不通
- .so 不能在 Windows Python 加载
- 解决方案：在 WSL 内建完整的开发环境，或交叉编译

### Bug 4：None/空指针传播
- C 中 NULL 返回值在 Python 端需要统一处理
- 建议约定：所有失败返回带 "Error:" 前缀的字符串

---

## 验证准则

1. **原语语义等价**：每个 P0/P1 原语在 C 中的行为必须与 Python 参考实现完全一致
2. **无内存泄漏**：`valgrind --leak-check=full ./dao_core_test` 不报错
3. **无崩溃**：边界输入（空字符串、超大值、特殊字符）不 segfault
4. **与现有 .ku 代码兼容**：442 个 thought 不改代码的前提下可运行

---

## 文件速查

| 文件 | 作用 |
|------|------|
| `D:\Tools\Dao\docs\道语言原语规范.md` | ABI 契约 — 必须遵守 |
| `D:\Tools\Dao\dao\runtime.py` | Python 参考实现（~3276行） |
| `D:\Tools\Dao\dao\dao_core.c` | 现有 C 共享库原型 |
| `D:\Tools\Dao\dao\dao_ipc.c` | 现有 C IPC 守护程序 |
| `D:\Tools\Dao\dao\dao_core_c.py` | Python ctypes 包装（未完成） |
| `D:\Tools\CppLearning\天书读道德经.ku` | 认知层验证示例 |
| `D:\Tools\Dao\dao\std\*.ku` | 标准库（~200个 thought） |
| `D:\Tools\Dao\dao\agent_core.ku` | 认知核心（任务分解） |
| `D:\Tools\Dao\dao\memory_system.ku` | 记忆系统 |
| `D:\Tools\Dao\dao\planner.ku` | 规划器 |
| `D:\Tools\Dao\dao\tool_registry.ku` | 工具注册表 |
| `~/.hermes/profiles/tianshu/config.yaml` | MCP 服务器配置 |

---

## 开发环境

```
OS: Windows 11 + WSL kali-linux
Python: 3.12.9 (Windows) + 3.13.7 (WSL)
C 编译器: gcc (Debian 14.3.0-5) 在 WSL
C 标准: C99
Python C 扩展: python3-dev (WSL 已装)
工作目录: D:\Tools\Dao\ (Windows) / /mnt/d/Tools/Dao/ (WSL)
```

编译命令：
```bash
# 共享库
gcc -shared -fPIC -o dao_core.so dao_core.c -lm -O2

# 独立测试
gcc -DDAO_STANDALONE -o dao_core_test dao_core.c -lm -O2
```

---

## 交付标准

当以下全部满足时，C 运行时才算完成：

1. ✅ P0 + P1 共 15 个原语通过语义等价测试
2. ✅ `valgrind --leak-check=full` 零错误
3. ✅ 可通过 `dao_eval()` 运行 `agent_core.ku` 的 `__decompose` 函数
4. ✅ Python 端可通过 ctypes 调用 C eval 获取正确结果
5. ✅ 内存管理无 hack（没有"进程退出 OS 回收"这种绕过）
