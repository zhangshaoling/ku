> **MIGRATION EVIDENCE** — This document records a legacy-phase audit/decision. It does not guide current Ku implementation. See [docs/README.md](README.md) for the authoritative document hierarchy.

# C VM Long-Running Recovery Guide

## Safety rule

Do not treat recycling a worker after a fixed number of requests as proof of memory safety.
The legacy arena was designed for process-exit cleanup. Persistent mode must remain opt-in
until request isolation, request-generation rollback, protocol framing, resource cleanup,
and long-running tests all pass.

The stable default is one C VM process per call. Set `DAO_CVM_PERSISTENT=1` only for bounded
testing. `DAO_CVM_MAX_WORKERS`, `DAO_CVM_MAX_REQUESTS`, and `DAO_CVM_MAX_PRIVATE_MB` are
secondary containment controls. `DAO_CVM_IDLE_SECONDS` reclaims workers that have not served
a request within the configured interval (default: 300 seconds).
Both isolated processes and persistent workers are sampled while starting and executing;
crossing `DAO_CVM_MAX_PRIVATE_MB` terminates the process immediately instead of waiting for
the request timeout. Windows uses private usage, Linux uses private pages from
`smaps_rollup` with RSS fallback, and macOS uses physical footprint with system RSS fallback.
Persistent mode fails closed when no memory sampler is available.

The regular `memory` profile excludes the task queue, and calls to `gap_to_task` and
task-queue thoughts use `memory_tasks`, which loads `task_queue.ku` before
`experience.ku`. Runtime profiles load validated per-module bytecode snapshots instead of
compiling standard-library source on every process start. A snapshot is accepted only when
its ABI, bootstrap fingerprint, source hash, artifact hash, module ID, and exports match.
Changed or missing snapshots fall back to source execution under the same timeout and memory
limits.

Regenerate snapshots with `python tools/generate_c_vm_module_snapshots.py`. Build the C VM
on Windows, Linux, or macOS with `python tools/build_dao_core.py` and an available C compiler.

MCP `tools/list` reads promoted-tool metadata directly from `experience.db` with a short
SQLite timeout. Listing tools must not compile a memory profile or create a C VM child.

## Required invariants

1. Request variables and compiler temporaries never enter the bootstrap/profile global env.
2. Each response is serialized before request-owned arena objects are released.
3. Worker stdout contains protocol frames only; Dao `print()` and diagnostics use stderr.
4. SQLite connections and other request-owned handles close on success and error paths.
5. MCP and CLI close every worker on EOF, normal exit, exceptions, and timeouts.

Increasing timeouts, lowering process priority, shortening worker lifetime, or calling the
process-wide `arena_freeall()` after a request does not satisfy these invariants.

## Delivery order

1. Keep persistent mode disabled by default and make all shutdown paths deterministic.
2. Introduce structured request/response envelopes and isolate request environments.
3. Roll arenas back to the post-bootstrap baseline after serializing each response.
4. Add pool capacity and private-memory recycling as defense in depth.
5. Run bounded regression tests, then the 30-minute or 10,000-request soak test.

Each step must remain independently reversible. Do not change persistent mode to the default
without recorded soak-test results showing no response corruption, residual child process,
resource exhaustion, or sustained post-warmup memory growth.

## Current acceptance status

- Frontend persistent-mode protocol, isolation, container rollback, resource cleanup, and
  1,000-request bounded-memory tests pass.
- MCP initialize, `tools/list`, and EOF exit complete without creating a C VM worker.
- All 17 runtime-profile modules have validated bytecode snapshots. On the current Windows
  host, core cold start is about 66 ms and hot P95 is 19.55 ms; memory cold start is about
  84 ms and hot P95 is 36.01 ms. Workers use about 18-30 MB private memory.
- Core, memory, memory_tasks, Tiandao, MCP, and experience-memory gateway tests pass.
- Different workers execute concurrently; closing a runtime interrupts an active worker and
  terminates its process group/tree.
- Snapshot generation still uses the Python build harness, and promoted-tool discovery still
  reads SQLite in Python. These remain K7 closure items rather than accepted final ownership.
- Windows is verified locally. Linux x64 and macOS arm64 build/runtime CI and the 30-minute or
  10,000-request admission run are still required.
- Persistent mode therefore remains experimental and disabled by default.
