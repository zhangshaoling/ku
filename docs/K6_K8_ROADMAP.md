# K6-K8 Upper-Layer Roadmap

This document extends the kernel guide, which formally defines K0-K6 but does not define
K7 or K8. It maps the existing executable-memory Stage A-E roadmap into numbered delivery
milestones without changing the rule that upper layers consume the kernel rather than
expanding its language ABI.

## K6: Governed Executable Memory

Goal: complete the reliable recall and callable-memory loop on the C VM path.

Acceptance:

- durable UTF-8 experience, data, graph, gap, and task memories;
- FTS recall, explainability, stable `dao://` locators, and promotion;
- callable promoted memories exposed through MCP;
- retention, archival, promotion retirement, index compaction, and migration policies;
- long-running C VM ownership audit with bounded memory behavior.

The lifecycle maintenance path uses a minimal `memory_lifecycle` profile while K7 module
snapshots are being completed. Runtime profiles now consume validated per-module bytecode
snapshots and no longer recompile standard-library source during normal startup. K6 still
requires the cross-platform soak and ownership evidence before completion.

## K7: Dao-Owned Modules And Discovery

Goal: remove Python as the owner of module assembly and promoted-tool schema discovery.

Acceptance:

- bytecode-level module identity and imports replace source concatenation;
- bootstrap/module generation is owned by Dao/C in production;
- promoted-memory schemas are discovered from Dao/C metadata;
- stable public standard-library APIs are separated from bootstrap helpers;
- Python remains only an optional harness, packaging layer, and compatibility client.

The first K7 delivery includes an experimental persistent `dao_core --serve` worker.
Production defaults to isolated one-shot processes until the long-running acceptance suite
passes. The worker uses request IDs, structured JSON envelopes, request-local environments,
request-generation arena rollback, and dedicated stdout framing. Request-count, worker-count,
and private-memory limits remain defense-in-depth controls, not proof of memory safety.
Python MCP tool listing currently reads promoted-tool rows directly from SQLite so discovery
does not start a memory-profile C VM. Dao/C-owned discovery remains a K7 acceptance item.
The checked-in snapshots are currently generated atomically by the Python build harness and
validated by ABI, bootstrap, source, and artifact hashes. This is the K7 startup-performance
delivery, not completion of Dao/C-owned snapshot generation or native import discovery.

## K8: Native Agent Gateway

Goal: run the complete thought-memory-tool loop through a native long-lived gateway.

Acceptance:

- C or Dao-owned MCP/JSON-RPC daemon with clean stdout framing and stderr diagnostics;
- native tool discovery and direct C VM memory record/search/call operations;
- retention and schema refresh work without restarting the gateway;
- process lifetime, cancellation, resource limits, and recovery are tested;
- the production loop from memory record to recall, execution, tool call, and updated memory
  has no Python semantic server dependency.

## Progress Accounting

Overall progress is the arithmetic mean of K0 through K8. A milestone percentage is based
on its acceptance bullets and proof tests, not on commit count or the currently active
subproject. This prevents K4/K5 completion from hiding unfinished K6-K8 work.
