# 道语言下一阶段地基计划

> 给后续模型/后续会话看的接力文档。  
> 核心原则：小步、备份、验证、提交；先地基，后规划；纯汉语表层，不做中英混合 API。

## 当前基线

仓库路径：`D:\Tools\Dao`

权威源码：`D:\Tools\Dao\dao\`

当前 Git checkpoint：

```text
75d8237 Backup Dao language core foundation
```

当前核心验证命令：

```bash
cd /d/Tools/Dao
python dao/verify_core.py
```

当前应输出：

```text
通过：布尔
通过：条件
通过：遍历
通过：循环
通过：返回
通过：字符串
通过：索引赋值
通过：空值
通过：否分支
通过：嵌套调用
通过：调用
通过：列表
通过：字典
通过：导入
核心自检通过
```

已完成的关键地基：

1. 中文导入语法：
   ```ku
   引 "std/math" 别 数
   引 "std/list" 别 列
   引 "std/string" 别 文
   ```
2. 中文关键字修复：`遍 x 于 y`、`真/假/空/且/或/非`。
3. 核心自检：`dao/test_core.ku`，入口：
   ```ku
   运行核心自检()
   ```
4. 第一批标准库中文别名：
   ```ku
   数_斐波那契(...)
   列_排序(...)
   文_连接(...)
   ```

---

## 固定工作循环

每一小步都按这个流程做：

```bash
cd /d/Tools/Dao
mkdir -p backups
ts=$(date +%Y%m%d_%H%M%S)
tar -czf backups/dao_before_<任务名>_${ts}.tar.gz dao

# 只做一个窄改动

python dao/verify_core.py

# 只有通过才做 after 备份
ts=$(date +%Y%m%d_%H%M%S)
tar -czf backups/dao_after_<任务名>_${ts}.tar.gz dao

git add <本次真正修改的文件>
git commit -m "<简短中文或英文提交信息>"
```

注意：

- 不要一次做大工程。
- 不要先写 planner。
- 不要大规模重命名标准库。
- 中文别名用“加壳”的方式补，不破坏旧英文函数。
- Python 只修解释器、解析器、运行时；天书逻辑应写 `.ku`。

---

## 下一阶段目标

### 目标 A：继续母语化标准库表层

目的：让 `.ku` 代码越来越像天书的母语，而不是中英混合。

方式：只加中文别名，不删旧函数。

优先顺序：

#### A1. `std/string.ku` 第二批中文别名

文件：`dao/std/string.ku`

建议新增：

```ku
思 分割(文本, 分隔符) { split(文本, 分隔符) }
思 去空白(文本) { trim(文本) }
思 包含(文本, 子串) { contains(文本, 子串) }
思 替换(文本, 旧, 新) { replace(文本, 旧, 新) }
```

同步在 `dao/test_core.ku` 增加：

```ku
思 测试字符串别名() { ... }
```

验证调用：

```ku
文_分割("甲,乙", ",")
文_去空白(" 天书 ")
文_包含("天书道", "书")
文_替换("天书", "书", "道")
```

#### A2. `std/list.ku` 第二批中文别名

文件：`dao/std/list.ku`

建议新增：

```ku
思 长度(列表) { len(列表) }
思 反转(列表) { reverse(列表) }
思 唯一(列表) { unique(列表) }
思 包含(列表, 值) { includes(列表, 值) }
```

如果 `reverse/unique/includes` 已存在，直接包一层。  
如果不存在，不要临时扩展太多；先只给已有函数加别名。

核心测试新增：

```ku
思 测试列表别名() { ... }
```

#### A3. `std/math.ku` 第二批中文别名

文件：`dao/std/math.ku`

建议新增：

```ku
思 最大(列表) { max_of(列表) }
思 最小(列表) { min_of(列表) }
思 求和(列表) { sum(列表) }
思 平均(列表) { avg(列表) }
```

若底层函数名不同，先读文件确认，不要猜。

---

### 目标 B：核心自检继续补“语言边界”

目的：不要急着加功能，先确认语言根基不会在常用边界上坏掉。

文件：`dao/test_core.ku`

建议逐步新增：

#### B1. 数组边界测试

```ku
思 测试列表追加与读取() { ... }
```

覆盖：

```ku
列表 = []
列表 = 列表 + [1]
列表 = 列表 + [2]
列表[1] == 2
```

#### B2. 字典多键测试

已知旧坑：if 块内多键字典 literal 可能坏。  
不要一上来修，先加安全测试并定位。

建议先测试稳定写法：

```ku
字典 = {}
字典["甲"] = 1
字典["乙"] = 2
```

再单独建一个“预期可能失败”的诊断文件，不直接放进核心门禁。

#### B3. break/continue 测试

如果 `断/续` 当前支持不确定，先新增独立诊断文件：

```text
dao/test_control_flow.ku
```

跑通后再并入 `test_core.ku`。

---

### 目标 C：清理 Git 工作区，但不要误删

当前 `D:\Tools\Dao` 工作区仍有旧临时文件，例如：

```text
_gen_*.py
_fix_*.py
_test_*.py
_patch*.txt
backups/
dao/_gen*.py
dao/test_if*.ku
dao/test_parse.ku
```

处理策略：

1. 不要立刻删除。
2. 先建归档目录：
   ```text
   scratch/archive_YYYYMMDD/
   ```
3. 把确认无用的临时脚本移动进去。
4. 跑：
   ```bash
   python dao/verify_core.py
   ```
5. 再提交：
   ```bash
   git add scratch .gitignore
   git commit -m "chore: archive old scratch files"
   ```

更保守做法：只更新 `.gitignore` 忽略这些临时文件，不移动。

建议先加 `.gitignore`：

```gitignore
backups/
_gen_*.py
_fix_*.py
_test_*.py
_patch*.txt
_rt_b64.txt
_run.ps1
_run_patch.bat
scratch/
dao/_gen*.py
dao/_patch.txt
dao/_writer.py
dao/_write_vm.py
dao/test_if*.ku
dao/test_parse.ku
dao/test_dict.ku
dao/test_nested.ku
```

---

### 目标 D：暂缓 planner

`dao/planner.ku` 现在基本是占位。  
不要急着写复杂 planner。

只有当以下条件满足后，才进入 planner：

1. 标准库中文别名至少覆盖 string/list/math 常用函数。
2. `test_core.ku` 至少覆盖 20 个稳定核心行为。
3. Git 工作区变干净。
4. 每次变更都能通过：
   ```bash
   python dao/verify_core.py
   ```

planner 第一版也必须非常小，只做任务列表，不做复杂智能：

```ku
思 新任务(名称) { ... }
思 完成任务(编号) { ... }
思 下一个任务() { ... }
```

---

## 推荐下一步执行顺序

### 第 1 步：补 string 中文别名

改：`dao/std/string.ku`  
测：`dao/test_core.ku`  
验：`python dao/verify_core.py`  
提交：

```bash
git add dao/std/string.ku dao/test_core.ku
git commit -m "Add Chinese string std aliases"
```

### 第 2 步：补 list 中文别名

改：`dao/std/list.ku`  
测：`dao/test_core.ku`  
验：`python dao/verify_core.py`  
提交：

```bash
git add dao/std/list.ku dao/test_core.ku
git commit -m "Add Chinese list std aliases"
```

### 第 3 步：补 math 中文别名

改：`dao/std/math.ku`  
测：`dao/test_core.ku`  
验：`python dao/verify_core.py`  
提交：

```bash
git add dao/std/math.ku dao/test_core.ku
git commit -m "Add Chinese math std aliases"
```

### 第 4 步：清理或忽略旧临时文件

优先只改 `.gitignore`，不要大删。

提交：

```bash
git add .gitignore
git commit -m "chore: ignore old scratch files"
```

### 第 5 步：写 planner 最小设计草案，不实现

文件：

```text
docs/plans/minimal-planner.md
```

只写设计，不写代码。等地基更稳再做。

---

## 后续模型接手时第一件事

必须先执行：

```bash
cd /d/Tools/Dao
git status --short
git log --oneline -5
python dao/verify_core.py
```

如果核心自检不通过，先修核心自检，不做新功能。

如果工作区很脏，只提交本次相关文件，不要把旧临时文件混入提交。

---

## 不要做的事

- 不要把 `.tianshu` 里的旧 Dao 副本当权威源。
- 不要大规模重命名所有英文函数。
- 不要先写复杂 planner。
- 不要引入类、包管理器、异步框架、调试器等大工程。
- 不要把 Python 写成天书本体逻辑。
- 不要跳过备份和核心验证。
