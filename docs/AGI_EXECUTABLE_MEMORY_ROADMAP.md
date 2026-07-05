# Dao AGI Executable Memory Roadmap

Dao is a language for future AGI, not a generic scripting layer.

Its core equation is:

```text
code = memory = data = thought
```

The project exists so an intelligent system can express, persist, recall,
execute, inspect, and evolve its own thoughts through one runtime substrate.

## 1. Product Definition

Dao is an executable memory language.

It must let an AGI treat every important artifact as the same kind of object:

- source code written by the agent
- structured memory recorded from experience
- data produced by tools, users, tests, or the world
- callable thoughts exposed through MCP or another tool surface
- bytecode executable by the C VM
- records that can be recalled later without relying on a chat window

The goal is not merely "a model remembers chat history." The goal is a durable
external brain where memory can run, code can be remembered, and data can become
thought.

Dao must develop its own language shape. It should not copy Python's mental
model and merely rename it. In Dao, the important unit is a thought-memory:
source that can be stored, data that can be called, and memory that can execute.
Features are valuable when they make that cycle tighter.

## 2. What "Thought" Means

A Dao thought must eventually have all of these faces:

```text
Dao source
  -> AST / structured form
  -> bytecode
  -> C VM execution
  -> persisted memory record
  -> recall result
  -> MCP/tool callable surface
  -> revised or derived thought
```

If one of these faces is missing, the system may still work as an intermediate
implementation, but it is not complete.

## 3. Current State

The current mainline has reached the first real C VM-backed memory runtime
stage:

- Dao source can run through the self-hosting frontend and C VM bootstrap path.
- `ku_eval`, `ku_call`, golden-path, and experience-memory MCP tools use the C
  VM gateway by default.
- Python semantic fallback is explicit debug behavior, not the default path.
- Experience memory, gaps, datasets, data memories, and task queues persist
  under `DAO_DATA_DIR`.
- C VM-backed memory recall is exposed through an FTS-backed
  `ku_recall_memory` MCP tool.
- Selected memory records can be promoted into stable callable thought/tool
  candidates through `ku_promote_memory` and called back through
  `ku_call_memory`.
- SQLite is the current durable store. It is acceptable as the first memory
  substrate and can later be joined by FTS, graph, vector, or distributed stores.

Python is still present as packaging, tests, fixture generation, MCP stdio glue,
and parity/debug scaffolding. That is acceptable only while semantic authority
continues moving into Dao source and the C VM.

## 4. Development Direction

### Stage A: Reliable Executable Memory

Goal: make model output, project decisions, failures, preferences, and code
changes persist as structured Dao memory records.

Required capabilities:

- record experience with type, topic, content, tags, source, and time
- retrieve memory by topic, tags, text, recency, and importance
- store enough provenance to know where a memory came from
- keep JSON-RPC stdout clean while runtime logs stay off the protocol channel

### Stage B: Fast Recall

Goal: make the AGI respond as if it remembers because relevant memories are
retrieved before it answers.

Required capabilities:

- SQLite FTS search for text recall
- topic and tag filters for exact recall
- recency and importance scoring
- MCP tools for record, search, promote, and explain-recall
- tests that prove UTF-8 Chinese memory survives roundtrip storage and recall

### Stage C: Callable Memory

Goal: important memories are not dead notes; they can become thought/tool
surfaces.

Required capabilities:

- promote selected memory records into callable thoughts
- expose stable promoted thoughts through MCP
- preserve the original memory record and link derived thoughts back to it
- distinguish factual memory, preference memory, procedure memory, and code
  memory
- make promoted memory feel like a native Dao thought, not a Python callback
  wrapper

### Stage D: Dao-Owned Bootstrap And Modules

Goal: reduce Python from semantic bridge to optional engineering harness.

Required capabilities:

- replace source-concatenation module imports with a stable module/bytecode ABI
- move more bootstrap image or bytecode generation into Dao itself
- make the C VM load, run, and inspect Dao modules without Python deciding
  meaning
- design module identity around persisted thoughts and memories, not only
  filename-based imports copied from another language

### Stage E: Native Agent Gateway

Goal: the AGI can use Dao memory through a native runtime gateway, not a Python
semantic server.

Required capabilities:

- C or Dao-backed MCP/JSON-RPC daemon
- native tool schema discovery for persisted thoughts
- direct C VM-backed memory record/search/call loop
- Python kept only for tests, packaging, and optional compatibility

## 5. Near-Term Build Order

The next implementation work should stay narrow:

1. Harden memory recall scoring, filters, and explainability.
2. Add promotion policy: decide which memories should become callable and why.
3. Expose promoted memories as dynamic MCP tool schemas, not only through the
   generic `ku_call_memory` entry point.
4. Add retention, compaction, and migration rules for long-lived memory stores.
5. Only then expand syntax or higher-level AGI behavior.

## 6. Non-Goals For Now

Do not spend the next phase on:

- decorative syntax changes that do not improve executable memory
- generic chatbot memory without Dao source/C VM ownership
- Python-only memory behavior that the C VM cannot verify
- broad "AGI framework" abstractions without persisted thought semantics
- replacing SQLite before recall, promotion, and callable memory are proven

## 7. Completion Standard

Dao reaches the first AGI-memory milestone when this loop is real:

```text
model output or user decision
  -> Dao memory record
  -> durable storage
  -> fast recall
  -> C VM-executed thought
  -> MCP callable tool
  -> updated memory
```

At that point, Dao is not just a language runtime. It is the beginning of an AGI
memory substrate: a system where code, memory, data, and thought are the same
evolving object.
