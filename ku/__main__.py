import sys

if len(sys.argv) > 1 and sys.argv[1] == "mcp":
    # ku mcp [ku_dir ...] — 启动 MCP 服务器
    sys.argv = [sys.argv[0]] + sys.argv[2:]
    from .mcp_server import main
    main()
else:
    from .runtime import main
    main()
