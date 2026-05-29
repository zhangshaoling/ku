"""
Ku MCP Server — 将 Ku thought 自动暴露为 MCP 工具

每个 .ku 文件中的 thought 自动成为 Claude Code 可调用的 MCP tool。
JSON-RPC 2.0 over stdio，兼容 Claude Code MCP 协议。

Usage:
    python -m ku.mcp_server [ku_dir ...]
    # 默认扫描 ku/std/ 目录
"""

import sys
import os
import json
import glob as _glob

# ── 编码处理 ──
if sys.platform == "win32":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# ── 路径设置 ──
KU_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(KU_DIR))

from ku.runtime import KuEnv, Thought, Node, parse_ku, _parse_expr


# ── MCP Protocol helpers ──

def rpc_result(req_id, result):
    msg = json.dumps({"jsonrpc": "2.0", "id": req_id, "result": result}, ensure_ascii=True)
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


def rpc_error(req_id, code, message):
    msg = json.dumps({"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}, ensure_ascii=True)
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


# ── 扫描 .ku 文件，提取 thought 定义 ──

def scan_ku_files(ku_dirs):
    """扫描目录中的 .ku 文件，解析出 thought 列表。

    返回: [(file_path, thought_name, params, description)]
    """
    thoughts = []
    for ku_dir in ku_dirs:
        if not os.path.isdir(ku_dir):
            continue
        for ku_file in sorted(_glob.glob(os.path.join(ku_dir, "*.ku"))):
            try:
                with open(ku_file, "r", encoding="utf-8") as f:
                    source = f.read()
                # 解析获取 thought 结构（不执行 body）
                parsed = parse_ku(source)
                for thought in parsed:
                    if thought.name.startswith("_"):
                        continue
                    desc = thought.doc or f"Ku thought: {thought.name}"
                    thoughts.append((ku_file, thought.name, thought.params, desc))
            except Exception:
                continue
    return thoughts


def scan_ku_file_thoughts(ku_file):
    """解析单个 .ku 文件，返回 Thought 对象列表。"""
    with open(ku_file, "r", encoding="utf-8") as f:
        source = f.read()
    return parse_ku(source)


def build_tool_definition(name, params, description):
    """将 thought 参数转为 MCP tool schema。"""
    properties = {}
    for p in params:
        properties[p] = {"type": "string", "description": f"参数 {p}"}
    return {
        "name": f"ku_{name}",
        "description": description,
        "inputSchema": {
            "type": "object",
            "properties": properties,
            "required": params,
        },
    }


# ── 工具调用处理器 ──

class KuToolHandler:
    """延迟加载 .ku 文件并调用 thought。"""

    def __init__(self, ku_file, thought_name, params):
        self.ku_file = ku_file
        self.thought_name = thought_name
        self.params = params
        self._loaded = False

    def _ensure_loaded(self):
        if self._loaded:
            return
        # parse_ku 会自动注册 thought 到 Thought.registry
        scan_ku_file_thoughts(self.ku_file)
        self._loaded = True

    def __call__(self, arguments):
        self._ensure_loaded()
        # 构建参数列表
        args = []
        for p in self.params:
            val = arguments.get(p)
            if val is None:
                args.append(None)
            elif isinstance(val, str):
                try:
                    if "." in val:
                        args.append(float(val))
                    else:
                        args.append(int(val))
                except ValueError:
                    args.append(val)
            else:
                args.append(val)

        thought = Thought.registry.get(self.thought_name)
        if thought is None:
            return {"error": f"Thought '{self.thought_name}' not found in registry"}

        result = thought.call(args)
        if isinstance(result, (str, int, float, bool, type(None))):
            return {"result": result}
        if isinstance(result, (dict, list)):
            return {"result": result}
        return {"result": str(result)}


# ── MCP Server main loop ──

def main():
    # 确定扫描目录
    if len(sys.argv) > 1:
        ku_dirs = sys.argv[1:]
    else:
        ku_dirs = [os.path.join(KU_DIR, "std")]

    # 扫描 thought
    thoughts = scan_ku_files(ku_dirs)

    # 构建 tool 定义和处理器
    tool_definitions = []
    tool_handlers = {}

    for ku_file, name, params, desc in thoughts:
        tool_def = build_tool_definition(name, params, desc)
        tool_definitions.append(tool_def)
        tool_handlers[tool_def["name"]] = KuToolHandler(ku_file, name, params)

    # 添加内置工具: ku_eval（直接求值 Ku 表达式）
    tool_definitions.append({
        "name": "ku_eval",
        "description": "直接求值 Ku 表达式或运行 Ku 代码片段",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "Ku 代码"},
            },
            "required": ["code"],
        },
    })

    def handle_ku_eval(arguments):
        code = arguments["code"]
        # 先加载标准库
        env = KuEnv()
        std_dir = os.path.join(KU_DIR, "std")
        if os.path.isdir(std_dir):
            for ku_file in sorted(_glob.glob(os.path.join(std_dir, "*.ku"))):
                try:
                    env.load(ku_file)
                except Exception:
                    pass
        # 尝试作为表达式求值
        try:
            ast = _parse_expr(code)
            t = Thought("__mcp_eval__", [], ast)
            result = t.call()
            # 简化结果
            if isinstance(result, (str, int, float, bool, type(None))):
                return {"result": result}
            if isinstance(result, (dict, list)):
                return {"result": result}
            return {"result": str(result)}
        except Exception:
            pass
        # 尝试作为 .ku 源码加载
        try:
            import tempfile
            tmp = os.path.join(tempfile.gettempdir(), "_mcp_eval.ku")
            with open(tmp, "w", encoding="utf-8") as f:
                f.write(code)
            thoughts = env.load(tmp)
            return {"thoughts_loaded": len(thoughts), "names": [t.name for t in thoughts]}
        except Exception as e:
            return {"error": str(e)}

    tool_handlers["ku_eval"] = handle_ku_eval

    # 添加内置工具: ku_list_thoughts
    tool_definitions.append({
        "name": "ku_list_thoughts",
        "description": "列出所有已加载的 Ku thought（包括内部 thought）",
        "inputSchema": {"type": "object", "properties": {}},
    })

    def handle_list_thoughts(arguments):
        all_thoughts = []
        for name, t in Thought.registry.items():
            all_thoughts.append({
                "name": name,
                "params": t.params,
                "executions": t.meta.get("executions", 0),
            })
        return {"thoughts": all_thoughts, "count": len(all_thoughts)}

    tool_handlers["ku_list_thoughts"] = handle_list_thoughts

    # ── MCP 协议主循环 ──
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue

        req_id = msg.get("id")
        method = msg.get("method")
        params = msg.get("params") or {}

        if method == "initialize":
            rpc_result(req_id, {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "ku-mcp", "version": "0.7.0"},
            })
        elif method == "notifications/initialized":
            pass
        elif method == "tools/list":
            rpc_result(req_id, {"tools": tool_definitions})
        elif method == "tools/call":
            tool_name = params.get("name")
            arguments = params.get("arguments", {})
            handler = tool_handlers.get(tool_name)
            if not handler:
                rpc_error(req_id, -32601, f"Unknown tool: {tool_name}")
                continue
            try:
                result = handler(arguments)
                rpc_result(req_id, {"content": [{"type": "text", "text": json.dumps(result, ensure_ascii=False)}]})
            except Exception as e:
                rpc_error(req_id, -32603, f"Tool error: {e}")
        elif method == "ping":
            rpc_result(req_id, {})
        else:
            rpc_error(req_id, -32601, f"Method not found: {method}")


if __name__ == "__main__":
    main()
