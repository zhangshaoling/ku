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

### Stage D: Dao-Owned Bootstrap And Modules

Goal: reduce Python from semantic bridge to optional engineering harness.

Required capabilities:

- replace source-concatenation module imports with a stable module/bytecode ABI
- move more bootstrap image or bytecode generation into Dao itself
- make the C VM load, run, and inspect Dao modules without Python deciding
  meaning

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

1. Add a C VM-backed `memory_recall` path.
2. Add SQLite FTS for fast text search over experience/data memories.
3. Add MCP tools for memory record and recall with clear schemas.
4. Add memory promotion: record -> stable thought/tool candidate.
5. Add callable-memory tests proving a persisted record can become a callable
   surface.
6. Only then expand syntax or higher-level AGI behavior.

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
