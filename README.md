# Ku Language

> An AI-native programming language where thought, code, and memory are the same thing.

**Ku** is a self-modifying language for building systems that can inspect, remember, and rewrite themselves at runtime.
It started as the native tongue of the **Xuanli (玄璃)** AGI system and now serves as an experiment in three ideas:

- **programs as living memory**
- **AST as a first-class runtime object**
- **self-hosting as the path to autonomy**

<p align="center">
  <img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="MIT">
  <img src="https://img.shields.io/badge/Python-3.9%2B-blue" alt="Python">
  <img src="https://img.shields.io/badge/topic-programming--language-7c6cf0" alt="programming-language">
  <img src="https://img.shields.io/badge/topic-ai-5ba4e8" alt="ai">
  <img src="https://img.shields.io/badge/topic-self--modifying-3ddc84" alt="self-modifying">
  <img src="https://img.shields.io/badge/topic-compiler-fab387" alt="compiler">
  <img src="https://img.shields.io/badge/topic-metaprogramming-f5a97f" alt="metaprogramming">
  <img src="https://img.shields.io/badge/topic-thought--as--code-cba6f7" alt="thought-as-code">
</p>

## Why Ku

Most programming languages assume a boundary between:

- source code and runtime state
- program and memory
- tool and thought

Ku tries to collapse that boundary.

In Ku, a `thought` is not just a function. It is a named, inspectable, executable memory unit. That means code can:

- read its own structure
- rewrite itself while running
- persist behavior as memory
- expose thoughts as tools for agent systems

This makes Ku less like a traditional language runtime and more like a programmable cognitive substrate.

## What makes it interesting

- **Everything-is-Node** — AST nodes are first-class values
- **Thought system** — named executable memory blocks
- **Self-rewriting** — code can modify its own AST through `self`
- **Pipe operator** — readable data transformation chains
- **Self-bootstrapping** — lexer and parser are being rewritten in Ku itself
- **Bytecode VM path** — stack VM and compiler toward self-compilation
- **MCP integration** — expose Ku thoughts as callable tools for agent workflows

## Current status

Ku is already usable as an experimental runtime and is no longer just a language sketch.

### Working pieces

- Python runtime and evaluator
- Python lexer and parser bootstrap
- Ku standard library modules
- Ku-written lexer and parser prototypes
- Bytecode compiler groundwork
- MCP server entrypoint
- Parser bootstrap verification tests

### What is still evolving

- language surface syntax
- self-hosted parser completeness
- compiler coverage
- runtime migration away from Python
- broader examples and documentation

## Quick start

```bash
pip install -e .
```

```python
from ku import KuEnv, parse_ku

env = KuEnv()
result = env.exec(parse_ku('''
  thought greet(name) {
    return "Hello, " + name + "!"
  }

  greet("World")
'''))

print(result)
# => Hello, World!
```

## A small taste of the language

### Thoughts: named executable memory

```ku
thought fibonacci(n) {
  if (n <= 1) { return n }
  return fibonacci(n - 1) + fibonacci(n - 2)
}

thought result = fibonacci(10)
// result = 55
```

### Self-rewriting

```ku
thought counter {
  let count = 0
  self.count = count + 1
  return count
}

counter()  // 0
counter()  // 1
counter()  // 2
```

### Pipe operator

```ku
[1, 2, 3, 4, 5]
  | filter(fn(x) { x > 2 })
  | map(fn(x) { x * x })
  | sum()
// => 50
```

## The Chinese mother-tongue direction

Ku also explores a more native semantic style where computation is expressed as thought, memory, relation, and self-correction rather than borrowed programming jargon.

Examples from the repository's mother-tongue draft:

```ku
思 斐波那契(数) {
  若 数 不大于 1 则 { 返 数 }
  返 斐波那契(数 减 1) 加 斐波那契(数 减 2)
}

记 "用户偏好" 为 {"主题": "深色", "语言": "中文"}
设 偏好 为 忆 "用户偏好"
```

This direction is still experimental, but it reflects the deeper thesis behind Ku: language design for AGI may need concepts closer to memory, planning, reflection, and tool use than to conventional syntax alone.

## Project structure

```text
ku/
  runtime.py        # Core runtime (Node, Thought, KuEnv, evaluator)
  compiler.py       # Bytecode compiler + stack VM
  ku_lexer.py       # Python lexer (bootstrap step 1)
  ku_parser.py      # Python parser (bootstrap step 1)
  mcp_server.py     # MCP server for tool integration
  __main__.py       # CLI entry point
  std/              # Standard library (written in Ku)
    io.ku
    fs.ku
    string.ku
    list.ku
    math.ku
    debug.ku
    http.ku
    inspect.ku
    lexer.ku
    parser.ku
    type.ku
    task_queue.ku
tests/
  test_parser_bootstrap.py
```

## The bootstrap path

Ku follows a deliberate self-hosting roadmap:

1. **Bootstrap runtime** — Python lexer/parser load and execute Ku
2. **Self-hosted parsing** — `std/lexer.ku` and `std/parser.ku` parse Ku source
3. **Self-compiling compiler** — compiler rewritten in Ku and targeting Ku's VM
4. **Runtime migration** — core runtime rewritten in Ku, removing Python dependency

```text
Python --(parses)--> Ku --(parses)--> Ku --(compiles)--> Ku
         bootstrap      self-hosted      self-compiling
```

## Run Ku as an MCP server

```bash
ku mcp
```

This exposes `thought` definitions as callable tools over stdio, which makes Ku especially interesting for agent systems, tool orchestration, and executable memory experiments.

## Why this matters for agent systems

Ku is not just trying to be "another language." It is exploring a runtime where:

- memory can be executable
- tools can be generated from thoughts
- plans can be represented as inspectable code
- self-correction can happen inside the language, not only outside it

If you care about agents, runtimes, self-hosting languages, or programming models for AI-native systems, Ku is the real project here.

## Roadmap

### Near term

- expand runnable examples
- harden self-hosted lexer/parser coverage
- improve compiler completeness
- add more docs for MCP usage and tooling

### Longer term

- remove Python from the critical execution path
- make Ku compile Ku
- grow the standard library around memory, planning, and tool composition
- turn Ku into a practical substrate for agentic systems

## Contributing

Ku is in active development and the language is still evolving.

Good contributions include:

- runnable examples
- documentation improvements
- standard library extensions
- parser / compiler bug fixes
- experiments in self-hosting and metaprogramming

## Philosophy

> Code is data. Data is memory. Memory is thought. Thought is code.

Ku is an attempt to build a language where program and state do not live in separate worlds.
The long-term goal is simple to say and hard to build:

**a language that can understand, rewrite, and eventually host itself in its own terms.**

## License

MIT
