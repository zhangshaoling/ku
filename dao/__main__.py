import sys


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "mcp":
        sys.argv = [sys.argv[0]] + sys.argv[2:]
        from .mcp_server import main as mcp_main
        return mcp_main()
    if len(sys.argv) > 1 and sys.argv[1] == "react":
        goal = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "help"
        from .runtime import ReactLoop
        loop = ReactLoop(goal=goal)
        result = loop.run()
        print("Result:", result)
        return 0
    if len(sys.argv) > 1 and sys.argv[1] == "plan":
        goal = " ".join(sys.argv[2:]) if len(sys.argv) > 2 else "help"
        from .runtime import TaskPlanner
        tasks = TaskPlanner.decompose(goal)
        for task in tasks:
            print("  [%s] %s (deps: %s)" % (task.priority.name, task.name, task.deps))
        return 0

    from .runtime import main as runtime_main
    return runtime_main()


if __name__ == "__main__":
    raise SystemExit(main())
