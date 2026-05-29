# Ku Language

**Ku** is an AI-native, self-modifying programming language where `thought = code = memory`.

Born as the native tongue of the [Xuanli](https://github.com/anthropics/claude-code) AGI system, Ku treats code as executable memory -- every expression is a thought that can be inspected, rewritten, and composed at runtime.

## Key Features

- **Everything-is-Node** -- AST nodes are first-class values, inspectable and rewritable
- **Self-bootstrapping** -- Lexer and parser written in Ku itself (dual Python/Ku implementation)
- **Thought system** -- Named code blocks that persist as memory, callable by name
- **Self-rewriting** -- The `self` keyword lets code modify its own AST at runtime
- **Pipe operator** -- `|` chains for readable data transformation
- **Bytecode compiler** -- Stack-based VM, targeting self-compilation
- **MCP integration** -- Expose Ku thoughts as MCP tools for Claude Code

## Quick Start

```bash
pip install -e .
```

```python
from ku import KuEnv, parse_ku

env = KuEnv()
env.exec(parse_ku('''
  thought greet(name) {
    return "Hello, " + name + "!"
  }
  greet("World")
'''))
# => "Hello, World!"
```

## Language Overview

### Thoughts (named code blocks)

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
  self.count = count + 1  // rewrites own AST
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

## Project Structure

```
ku/
  runtime.py        # Core runtime (Node, Thought, KuEnv, evaluator)
  compiler.py       # Bytecode compiler + stack VM
  ku_lexer.py       # Python lexer (bootstrap step 1)
  ku_parser.py      # Python parser (bootstrap step 1)
  mcp_server.py     # MCP server for Claude Code integration
  __main__.py       # CLI entry point
  std/              # Standard library (written in Ku)
    io.ku           # I/O operations
    fs.ku           # Filesystem
    string.ku       # String manipulation
    list.ku         # List operations (map, filter, reduce...)
    math.ku         # Math library
    debug.ku        # Debug utilities
    http.ku         # HTTP client
    inspect.ku      # Metaprogramming / introspection
    lexer.ku        # Self-bootstrapping lexer
    parser.ku       # Self-bootstrapping parser
    type.ku         # Type checking
    task_queue.ku   # Priority task scheduler
tests/
  test_parser_bootstrap.py  # Python vs Ku parser verification
```

## The Bootstrap Path

Ku follows a deliberate self-bootstrapping strategy:

1. **Phase 1** (current) -- Python lexer/parser bootstrap the language
2. **Phase 2** -- `std/lexer.ku` and `std/parser.ku` parse Ku source (proven: 22/22 test cases pass)
3. **Phase 3** -- Bytecode compiler written in Ku, self-compiling
4. **Phase 4** -- Runtime rewritten in Ku, Python dependency removed

```
Python --(parses)--> Ku --(parses)--> Ku --(compiles)--> Ku
         bootstrap      self-hosted      self-compiling
```

## MCP Server

Run Ku as an MCP tool server for Claude Code:

```bash
ku mcp
```

This exposes all `thought` definitions as callable MCP tools over stdio (JSON-RPC 2.0).

## Contributing

Ku is in active development. The language spec is still evolving. Contributions welcome:

- Bug reports and feature requests via Issues
- Standard library modules
- Documentation and examples
- Compiler optimizations

## License

MIT

## Philosophy

> "Code is data. Data is memory. Memory is thought. Thought is code."

Ku is not just a programming language -- it's an experiment in building a system where the boundary between program and state dissolve. Every thought is simultaneously executable code, inspectable data, and persistent memory.

The ultimate goal: a language that can rewrite itself completely, from lexer to runtime, in its own terms.
