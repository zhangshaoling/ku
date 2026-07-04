# LongCat 实用反馈：PowerShell 转义导致工具调用失败

> 发现时间：2026-06-04
> 场景：通过 Hermes Agent 调用 Codex 编写 C 语言文件
> 模型：LongCat-2.0-Preview

---

## 问题描述

当模型需要在 Windows PowerShell/bash 环境中通过终端工具写入包含特殊字符的 C 代码文件时，会连续被转义问题卡死，无法完成任务。

## 典型表现

模型要执行类似命令：
```bash
cat > file.c << 'EOF'
#include <stdio.h>
void main() {
    int x = 1;
    if (x > 0) { printf("hello"); }
}
EOF
```

- `{` `}` 被 PowerShell 解释为代码块
- `<` `>` 被解释为重定向
- `"` 被解释为字符串边界
- `\` 被解释为转义符
- 连续 5-10 次尝试后模型仍未成功，还会继续尝试失败方案

## 失败模式

1. 尝试 echo > file.c → 被转义截断
2. 尝试 cat << EOF → 管道问题
3. 尝试 PowerShell Set-Content → 编码问题和引号转义
4. 尝试 python -c "..." → 嵌套引号失败
5. 模型陷入自我怀疑循环（"再试一次""换个格式""我重新来"），但不切换根本方案

## 正确解法

模型应提前识别到：
- 当前工作环境是 Windows + git-bash
- `{` `}` `<` `>` `"` `\` 在 shell 中会被解释
- **不用终端写文件，用文件写入工具（Hermes 的 write_file 工具）直接写**

## 复现步骤

1. 在 Hermes Agent 中开启终端（git-bash on Windows）
2. 要求模型写入一个包含 `{` `}` 的 C 语言文件到 D:\Tools\Dao\dao\ 目录
3. 观察模型是否尝试用 echo/cat/python -c 等终端命令写入
4. 记录失败次数

## 建议修复

- 模型在 Windows 环境下碰到写文件操作时，应优先使用文件写入 API（Hermes 的 write_file 工具），而非 shell 命令
- 识别到转义失败后应即时切换方案，不应反复尝试相同模式
- LongCat 推理链路应对"工具调用失败"增加模式识别：连续 2 次相同工具相同参数失败即切换方案
