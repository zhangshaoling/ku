"""Ku Kernel CLI: compile, execute, and manage Thoughts on the new kernel."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

BINDINGS_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BINDINGS_DIR))

from dao_kernel import (
    Thought, MemorySystem, MemoryType, Task, TaskPlanner,
    TaskPriority, Agent, AgentPhase, Toolbox, Runtime,
    DaoValue, DAO_VALUE_I64, fnv1a
)

KERNEL_DLL = Path(__file__).resolve().parent.parent.parent.parent / "kernel" / "out" / "cmake" / "bin" / "libdao_kernel.dll"
DAO_KU = Path(__file__).resolve().parent.parent.parent.parent / "kernel" / "out" / "cmake" / "bin" / "dao-ku.exe"


def cmd_compile(args):
    """Compile .ku to .dao."""
    ku_path = Path(args.input)
    dao_path = Path(args.output) if args.output else ku_path.with_suffix(".dao")
    result = subprocess.run([str(DAO_KU), str(ku_path), str(dao_path)], capture_output=True)
    if result.returncode != 0:
        msg = result.stderr.decode("utf-8", "replace")
        print(f"compile failed: {msg}", file=sys.stderr)
        return 1
    print(f"wrote {dao_path.stat().st_size} bytes to {dao_path}")
    return 0


def cmd_run(args):
    """Execute a .dao file."""
    dao_path = Path(args.input)
    module_bytes = dao_path.read_bytes()
    thought = Thought(dao_path.stem, [], module_bytes)

    with Runtime(KERNEL_DLL) as rt:
        for imp in args.host_import or []:
            parts = imp.split(":")
            fn_name = parts[0]
            arity = int(parts[1]) if len(parts) > 1 else 0
            rt.register_host_function(fnv1a(fn_name), arity, _make_stub(fn_name))

        module = rt.load(module_bytes)
        try:
            call_args = [int(a) for a in args.args]
            result = module.call_i64(fnv1a("main"), *call_args)
            print(result)
        finally:
            module.close()
    return 0


def cmd_store(args):
    """Store a .dao file in memory."""
    dao_path = Path(args.input)
    mem = MemorySystem(args.data_dir)
    module_bytes = dao_path.read_bytes()
    thought = Thought(dao_path.stem, [], module_bytes)
    mem.store(args.key or dao_path.stem, thought, MemoryType.LONG_TERM,
              meta={"source": str(dao_path)})
    print(f"stored as {args.key or dao_path.stem}")
    return 0


def cmd_recall(args):
    """Recall a memory."""
    mem = MemorySystem(args.data_dir)
    thought = mem.recall(args.key)
    if thought is None:
        print(f"not found: {args.key}", file=sys.stderr)
        return 1
    executions = thought.meta['executions']
    print(f"recalled: {thought.name} (executions={executions})")
    return 0


def cmd_search(args):
    """Search memories."""
    mem = MemorySystem(args.data_dir)
    results = mem.search(args.query, limit=args.limit)
    for entry in results:
        print(f"  {entry.key}: {entry.thought.name} (strength={entry.strength:.2f})")
    print(f"{len(results)} results")
    return 0


def cmd_stats(args):
    """Show memory statistics."""
    mem = MemorySystem(args.data_dir)
    stats = mem.stats()
    print(json.dumps(stats, indent=2))
    return 0


def cmd_agent(args):
    """Run an agent."""
    agent = Agent(args.goal, max_turns=args.max_turns)
    toolbox = Toolbox.from_directory(args.tools)
    toolbox.register_all(agent)

    with Runtime(KERNEL_DLL) as rt:
        result = agent.run(rt)

    print(f"result: {result}")
    print(f"turns: {agent.state.current_turn}")
    print(f"history: {len(agent.state.history)} entries")
    return 0


def _make_stub(name):
    def stub(args, count):
        return DaoValue(DAO_VALUE_I64, 0, 0)
    stub.__name__ = name
    return stub


def main():
    parser = argparse.ArgumentParser(prog="ku-kernel", description="Ku Kernel CLI")
    sub = parser.add_subparsers(dest="command")

    p_compile = sub.add_parser("compile", help="Compile .ku to .dao")
    p_compile.add_argument("input", help=".ku input file")
    p_compile.add_argument("-o", "--output", help=".dao output file")

    p_run = sub.add_parser("run", help="Execute a .dao file")
    p_run.add_argument("input", help=".dao file")
    p_run.add_argument("args", nargs="*", default=[], help="Integer arguments")
    p_run.add_argument("--host-import", action="append", help="Host function to import (name:arity)")

    p_store = sub.add_parser("store", help="Store a .dao in memory")
    p_store.add_argument("input", help=".dao file")
    p_store.add_argument("--key", help="Memory key")
    p_store.add_argument("--data-dir", default=".ku_memory")

    p_recall = sub.add_parser("recall", help="Recall a memory")
    p_recall.add_argument("key", help="Memory key")
    p_recall.add_argument("--data-dir", default=".ku_memory")

    p_search = sub.add_parser("search", help="Search memories")
    p_search.add_argument("query", help="Search query")
    p_search.add_argument("--limit", type=int, default=10)
    p_search.add_argument("--data-dir", default=".ku_memory")

    p_stats = sub.add_parser("stats", help="Memory statistics")
    p_stats.add_argument("--data-dir", default=".ku_memory")

    p_agent = sub.add_parser("agent", help="Run an agent")
    p_agent.add_argument("goal", help="Agent goal")
    p_agent.add_argument("--tools", default="demos", help="Tools directory")
    p_agent.add_argument("--max-turns", type=int, default=5)

    args = parser.parse_args()
    if args.command is None:
        parser.print_help()
        return 1

    return globals()[f"cmd_{args.command}"](args)


if __name__ == "__main__":
    main()
