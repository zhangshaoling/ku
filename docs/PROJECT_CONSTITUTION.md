# Ku Project Constitution

> Project law for humans and agents working on Ku.
>
> North star: **thought = code = memory**.

## 1. Identity

Ku is an AI-native machine-language system for executable memory.

It is not a generic scripting language, not a note-taking format, and not a pile of automation scripts. The project exists to explore one idea with engineering discipline:

```text
thought = code = memory
```

A thought is simultaneously:

- executable behavior
- inspectable structure
- persistent memory
- structured data
- composable tool surface
- material that can eventually rewrite itself

## 1.1 Ku Identity Law

Ku must not become Python with Chinese keywords.

The language's native object is not "a function plus variables." The native
object is a Ku thought-memory:

```text
Ku source = executable thought = durable memory = structured data
```

That means new syntax, standard-library APIs, MCP tools, and storage schemas
should prefer one unified lifecycle:

```text
write thought
  -> inspect as structure
  -> persist as memory
  -> recall as data
  -> execute in the C VM
  -> expose as a tool
  -> revise into a new thought
```

Do not copy Python concepts just because they are familiar. A proposed feature
must explain how it helps Ku make memory executable, code persistent, or data
callable. If it only recreates Python classes, modules, decorators, exceptions,
or package habits without serving this lifecycle, it is not a Ku feature yet.

## 2. Naming Law

The language system and product have one current name: **Ku**.

- `dao_*`, Dao Binary Module, `dao/`, environment variables and historical tool names are existing implementation or compatibility identifiers.
- They do not define a second language and must not create a second product roadmap.
- New production-kernel work belongs under `kernel/`.
- Existing `dao/` and `ku/` behavior is migration input unless a task explicitly targets compatibility.
- Internal names are not batch-renamed without a separate ABI and migration decision.

The authoritative naming and document order is defined in `docs/KU_NAMING_AND_AUTHORITY.md`.

## 3. Non-Negotiable North Star

Every major feature must serve at least one of these goals:

1. Make code inspectable as data.
2. Make memory executable as thought.
3. Move the runtime closer to self-hosting.
4. Make thoughts callable by agents through a stable tool interface.
5. Reduce dependence on Python as the semantic authority.

If a feature does none of these, it does not belong in this repository.


## 4. Physical World Direction

Ku's long-term direction includes the physical world. A thought that is
executable behavior, inspectable structure, and persistent memory should
eventually be callable by robots and embodied agents — not only by software
agents.

This means:

- The C ABI and FFI are not optional extras; they are the bridge between
  Ku thoughts and physical sensors, actuators, and control systems.
- Deterministic execution matters. Physical actions have real-world
  consequences that cannot be undone with `git revert`.
- Safety and verification matter. Code that moves a physical system must
  be held to a higher verification standard than code that moves pixels.

This direction does not change the current bootstrap path. It does mean
that FFI coverage, C ABI stability, and verification rigor are first-class
citizens in the roadmap — not afterthoughts.
## 5. Current Bootstrap Path

The official route is:

```text
Python bootstrap
  -> Ku lexer and parser
  -> bytecode compiler
  -> C VM execution
  -> MCP callable thought runtime
  -> self-hosted compiler/runtime pieces
```

The project is allowed to use Python as scaffolding, but Python is not the final home of the language semantics.

Python may remain for:

- test harnesses
- fixture generation
- migration bridges
- packaging
- debugging support

Python should shrink over time as semantic authority moves into Ku source and the new kernel.

## 6. Repository Boundary

This repository may contain:

- language runtime code
- parser, lexer, compiler, VM, standard library
- generated fixtures needed for deterministic tests
- MCP server and tool exposure code
- project documentation and design records
- tests and build scripts for the language stack

This repository must not contain:

- unrelated operations scripts
- cloud tunnel setup scripts
- personal machine repair scripts
- one-off debug scripts at repo root
- API keys, OAuth tokens, client secrets, passwords
- local databases, caches, logs, process state, screenshots
- compiled binaries unless explicitly justified as release artifacts

Temporary experiments belong in `scratch/`, `backups/`, or outside the repository. Repeatable tools belong in `tools/`. Tests belong in `tests/`. Architecture records belong in `docs/`.

## 7. Active Core Boundaries

The new production core is:

```text
kernel/
  include/dao/          versioned C/C++ ABI and module format identifiers
  src/                  loader, verifier, Register VM, builder and migration compiler
  selfhost/             Ku self-hosted compiler
  stdlib/               migrated Ku standard library
  tests/                native conformance and integration tests
```

The legacy migration inputs are:

```text
dao/
  dao_core.c
  compiler.py
  dao_lexer.py
  dao_parser.py
  runtime.py
  mcp_server.py
  std/*.ku
```

Earlier compatibility and historical code lives under:

```text
ku/
```

Generated fixtures live under:

```text
demos/
```

Generation scripts live under:

```text
tools/
```

A new file should enter one of these regions for a clear reason. If it does not fit, stop and define the boundary first.

## 8. Engineering Discipline

Behavior beats ambition.

A change is not real until at least one of the following proves it:

- a passing test
- a deterministic demo fixture
- a C VM execution result
- a documented MCP call
- a before/after parity check

Avoid vague progress claims like "self-hosting improved" unless the exact executable path is named.

Prefer statements like:

```text
dao/std/parser.ku parses X and matches Python parser output on Y tests.
```

or:

```text
dao_core.c executes fixture Z and returns result R.
```

## 9. Test Law

The minimum acceptable verification for core changes is:

```bash
uv run --with pytest pytest -q
```

If native C compiler support is missing, C VM integration tests may skip, but they must skip explicitly and honestly.

New semantic behavior should include parity tests where possible:

- legacy runtime vs Ku new kernel
- Python compiler harness vs C VM execution
- generated fixture vs source behavior
- MCP tool call vs direct runtime call

No AI or human should declare the project healthy without saying what actually ran.

## 10. C VM Law

The C VM is the future execution substrate.

Rules:

- `dao_core.c` should stay deterministic and testable.
- Builtins added to the C VM must have Python or runtime parity coverage when possible.
- Memory ownership changes must be paired with leak or lifecycle checks.
- Binary build outputs such as `dao_core` and `dao_core.exe` are local artifacts unless deliberately released.
- SQLite or other vendored C dependencies must be documented and justified.

The C VM should not become an unreviewable dumping ground. If it grows too large, split by subsystem.

## 11. Self-Hosting Law

Self-hosting is a direction, not a slogan.

Each self-hosting milestone must name:

- what source is written in Ku
- what parses it
- what compiles it
- what executes it
- what test proves equivalence

Valid milestones look like:

```text
std/lexer.ku tokenizes fixture A and matches Python lexer output.
std/parser.ku parses fixture B and matches Python parser AST.
std/compiler.ku emits bytecode C and C VM executes it to result D.
```

Invalid milestones look like:

```text
The language is basically self-hosting now.
```

## 12. MCP Law

MCP is not an add-on; it is one of the reasons the project exists.

Ku thoughts should become callable tools for agents. The MCP layer must preserve:

- JSON-RPC framing on stdout
- runtime logs on stderr
- stable tool names
- explicit input schemas
- deterministic return payloads

An MCP tool should not hide errors. If a thought fails, the caller should receive structured failure information.

## 13. Documentation Law

Documentation must separate:

- implemented behavior
- partially implemented behavior
- planned behavior
- philosophy

Philosophy is welcome, but it must not impersonate implementation.

Every major roadmap document should answer:

1. What works now?
2. What is scaffolded by Python?
3. What runs in the C VM?
4. What remains speculative?
5. What command verifies the claim?

## 14. AI Collaboration Law

AI agents working on this project must not re-invent the project every session.

Before changing code, an agent should understand:

- the north star: `thought = code = memory`
- the active path: `Ku source -> recovery/self-host compiler -> verified module -> Register VM -> Host/MCP`
- the boundary: language runtime only, not unrelated ops scripts
- the verification command for the touched area

Agents should work on narrow tasks. Good prompts are specific:

```text
Only fix C VM list builtin parity. Do not touch parser or README.
```

Bad prompts are vague:

```text
Improve the language.
```

AI-generated changes must be smaller than the reviewer's ability to understand them.

## 15. File Hygiene Law

Do not commit these unless explicitly justified:

```text
*.db
*.sqlite
*.log
*.tmp
*.bak
*.pyc
__pycache__/
.pytest_cache/
.venv/
venv/
dao/dao_core
dao/dao_core.exe
scratch/
backups/
```

Root-level one-off scripts are presumed wrong. Put them in `tools/` only if they are repeatable and documented.

## 16. Release Definition

A meaningful release is not a pile of commits. A meaningful release has:

- a named version
- a README section matching reality
- a passing test command
- a short demo command
- a clear statement of Python scaffolding vs legacy C VM vs Ku new-kernel responsibility

For example:

```text
Ku v0.9:
- Ku package is included in Python packaging.
- C VM source is included but binaries are local artifacts.
- MCP server exposes runtime tools.
- Tests pass without native compiler; C VM integration skips honestly when compiler is absent.
```

## 17. Near-Term Definition of Success

The next successful project state is:

```text
One Ku source file
  -> parsed by the Ku frontend
  -> compiled to bytecode
  -> executed by dao_core.c
  -> exposed through MCP
  -> verified in CI
  -> explained in README without mythology outrunning reality
```

When this exists, the project has crossed from idea to working AI machine-language prototype.

## 18. Final Rule

Do not optimize for sounding profound.

Optimize for making one more piece of the equation real:

```text
thought = code = memory
```
