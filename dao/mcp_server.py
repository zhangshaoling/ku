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
import contextlib

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

from dao.runtime import DaoEnv, Thought, Node, parse_道, _parse_expr
from dao.c_vm_runtime import CVMRuntime


# ── MCP Protocol helpers ──

@contextlib.contextmanager
def runtime_output_to_stderr():
    """Keep stdout reserved for JSON-RPC frames while Dao runtime code runs."""
    original_stdout = sys.stdout
    sys.stdout = sys.stderr
    try:
        yield
    finally:
        sys.stdout = original_stdout

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
                parsed = parse_道(source)
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
    return parse_道(source)


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

class DaoToolHandler:
    """延迟加载 .ku 文件并调用 thought。"""

    def __init__(self, ku_file, thought_name, params):
        self.ku_file = ku_file
        self.thought_name = thought_name
        self.params = params
        self._loaded = False

    def _ensure_loaded(self):
        if self._loaded:
            return
        # parse_道 会自动注册 thought 到 Thought.registry
        with runtime_output_to_stderr():
            scan_ku_file_thoughts(self.ku_file)
        self._loaded = True

    def __call__(self, arguments):
        self._ensure_loaded()
        # 构建参数列表
        args = []
        for p in self.params:
            args.append(arguments.get(p))

        thought = Thought.registry.get(self.thought_name)
        if thought is None:
            return {"error": f"Thought '{self.thought_name}' not found in registry"}

        with runtime_output_to_stderr():
            result = thought.call(args)
        if isinstance(result, (str, int, float, bool, type(None))):
            return {"result": result}
        if isinstance(result, (dict, list)):
            return {"result": result}
        return {"result": str(result)}


# ── MCP Server main loop ──

def main():
    raw_args = sys.argv[1:]
    expose_thought_tools = False
    if "--expose-thought-tools" in raw_args:
        expose_thought_tools = True
        raw_args = [arg for arg in raw_args if arg != "--expose-thought-tools"]

    # 确定扫描目录。默认只加载 dao/std，但不默认把每个 thought 展成 MCP tool。
    ku_dirs = raw_args or [os.path.join(KU_DIR, "std")]

    # 构建 tool 定义和处理器
    tool_definitions = []
    tool_handlers = {}
    c_vm_runtime = CVMRuntime()

    def simplify_result(result):
        if isinstance(result, (str, int, float, bool, type(None))):
            return {"result": result}
        if isinstance(result, (dict, list)):
            return {"result": result}
        return {"result": str(result)}

    def load_runtime_env():
        env = DaoEnv()
        with runtime_output_to_stderr():
            for ku_dir in ku_dirs:
                if not os.path.isdir(ku_dir):
                    continue
                for ku_file in sorted(_glob.glob(os.path.join(ku_dir, "*.ku"))):
                    try:
                        env.load(ku_file)
                    except Exception:
                        pass
        return env

    def coerce_arg(val):
        if isinstance(val, str):
            try:
                if "." in val:
                    return float(val)
                return int(val)
            except ValueError:
                return val
        return val

    def is_expr_candidate(code):
        stripped = code.strip()
        if "\n" in stripped or "\r" in stripped:
            return False
        if stripped.startswith("思 "):
            return False
        if "=" in stripped and not any(op in stripped for op in ["==", "!=", ">=", "<="]):
            return False
        return True

    # 添加内置工具: ku_eval（直接求值 Ku 表达式）
    tool_definitions.append({
        "name": "ku_eval",
        "description": "直接求值 Ku 表达式或运行 Ku 代码片段",
        "inputSchema": {
            "type": "object",
            "properties": {
                "code": {"type": "string", "description": "Ku 代码"},
                "profile": {
                    "type": "string",
                    "description": "C VM runtime profile: core|memory|semantic|frontend，默认 core",
                },
            },
            "required": ["code"],
        },
    })

    def handle_ku_eval(arguments):
        code = arguments["code"]
        profile = arguments.get("profile") or "core"

        # M5c：ku_eval 默认走 C VM（dao_core.exe），与 ku_call/经验工具一致。
        # C VM 在源码层完成 lex -> parse_tokens -> compile_ast -> run_bytecode，
        # 既支持裸表达式，也支持 thought 定义 + 末尾表达式。
        if c_vm_runtime.binary.exists():
            result = c_vm_runtime.eval_code(code, profile=profile)
            if not result.ok:
                raise RuntimeError(result.error or result.stderr or result.stdout or "C VM execution failed")
            return simplify_result(result.value)

        # Fallback：仅当原生 C VM 二进制缺失时，退回 Python 解释路径（对拍/引导用）。
        env = load_runtime_env()
        ast = None
        if is_expr_candidate(code):
            try:
                ast = _parse_expr(code)
            except Exception:
                ast = None

        if ast is not None:
            t = Thought("__mcp_eval__", [], ast)
            with runtime_output_to_stderr():
                result = t.call()
            return simplify_result(result)

        try:
            from dao.compiler import compile_道, run_bytecode
            with runtime_output_to_stderr():
                bytecode = compile_道(code)
        except Exception:
            bytecode = None

        if bytecode is not None:
            with runtime_output_to_stderr():
                result = run_bytecode(bytecode, env)
            return simplify_result(result)

        try:
            with runtime_output_to_stderr():
                thoughts = env.load(code)
            return {"thoughts_loaded": len(thoughts), "names": [t.name for t in thoughts]}
        except Exception as e:
            return {"error": str(e)}

    tool_handlers["ku_eval"] = handle_ku_eval

    # 添加内置工具: ku_call（少量 schema 覆盖任意 thought 调用）
    tool_definitions.append({
        "name": "ku_call",
        "description": "按名字调用已加载的 Ku thought，避免把每个 thought 展成单独 MCP tool",
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string", "description": "thought 名称，可带或不带 ku_ 前缀"},
                "arguments": {
                    "type": "object",
                    "description": "参数对象；key 使用 thought 参数名",
                    "additionalProperties": True,
                },
                "profile": {
                    "type": "string",
                    "description": "C VM runtime profile: core|memory|semantic|frontend，默认 core",
                },
            },
            "required": ["name"],
        },
    })

    def handle_ku_call(arguments):
        load_runtime_env()
        name = arguments.get("name", "")
        if name.startswith("ku_"):
            name = name[3:]
        call_args = arguments.get("arguments", {})
        thought = Thought.registry.get(name)
        if thought is None:
            return {"error": f"Thought '{name}' not found in registry"}
        memory_thoughts = {
            "experience_init",
            "experience_record",
            "gap_record",
            "code_gap_record",
            "code_data_gap_record",
            "dataset_record",
            "data_memory_record",
            "gap_list_open",
            "gap_resolve",
            "experience_search",
            "experience_stats",
            "gap_to_task",
            "init_db",
            "submit",
            "claim_next",
            "complete",
            "list_tasks",
            "cancel",
            "get_pending_count",
            "routing_suggestion",
        }
        profile = arguments.get("profile") or ("memory" if name in memory_thoughts else "core")
        result = c_vm_runtime.call_thought(name, call_args, params=thought.params, profile=profile)
        if not result.ok:
            raise RuntimeError(result.error or result.stderr or result.stdout or "C VM execution failed")
        return simplify_result(result.value)

    tool_handlers["ku_call"] = handle_ku_call

    # 添加内置工具: ku_list_thoughts
    tool_definitions.append({
        "name": "ku_list_thoughts",
        "description": "列出所有已加载的 Ku thought（包括内部 thought）",
        "inputSchema": {"type": "object", "properties": {}},
    })

    def handle_list_thoughts(arguments):
        load_runtime_env()
        all_thoughts = []
        for name, t in Thought.registry.items():
            all_thoughts.append({
                "name": name,
                "params": t.params,
                "executions": t.meta.get("executions", 0),
            })
        return {"thoughts": all_thoughts, "count": len(all_thoughts)}

    tool_handlers["ku_list_thoughts"] = handle_list_thoughts

    tool_definitions.append({
        "name": "ku_golden_path",
        "description": "Run the checked-in Dao golden path demo through the C VM: source -> frontend -> bytecode -> result",
        "inputSchema": {"type": "object", "properties": {}},
    })

    def handle_golden_path(arguments):
        demo_path = os.path.join(os.path.dirname(KU_DIR), "demos", "golden_path.ku")
        with open(demo_path, "r", encoding="utf-8") as f:
            source = f.read()
        result = c_vm_runtime.eval_code(source, profile="frontend")
        if not result.ok:
            raise RuntimeError(result.error or result.stderr or result.stdout or "C VM execution failed")
        return simplify_result(result.value)

    tool_handlers["ku_golden_path"] = handle_golden_path

    # ── 经验记忆网关工具 ──
    # 让运行中的智能体把“尝试了什么 / 缺什么 / 下一步补什么”落库，
    # 而不是只在对话里说。底层是 dao/std/experience.ku（SQLite）。

    def call_c_vm_memory_thought(name, args):
        result = c_vm_runtime.call_thought(name, args, profile="memory")
        if not result.ok:
            raise RuntimeError(result.error or result.stderr or result.stdout or "C VM execution failed")
        return simplify_result(result.value)

    tool_definitions.append({
        "name": "ku_record_experience",
        "description": "记录一条运行时经验。kind: attempt|observation|gap|dataset|code_gap|code_data_gap|data_memory",
        "inputSchema": {
            "type": "object",
            "properties": {
                "kind": {"type": "string", "description": "经验类别"},
                "topic": {"type": "string", "description": "主题/标题"},
                "context": {"type": "string", "description": "上下文"},
                "input": {"type": "string", "description": "输入/尝试内容"},
                "output": {"type": "string", "description": "结果/输出"},
                "missing": {"type": "string", "description": "缺什么"},
                "next_action": {"type": "string", "description": "下一步动作"},
                "tags": {"type": "string", "description": "逗号分隔标签"},
            },
            "required": ["kind", "topic"],
        },
    })

    def handle_record_experience(arguments):
        return call_c_vm_memory_thought("experience_record", [
            arguments.get("kind"),
            arguments.get("topic"),
            arguments.get("context"),
            arguments.get("input"),
            arguments.get("output"),
            arguments.get("missing"),
            arguments.get("next_action"),
            arguments.get("tags"),
        ])

    tool_handlers["ku_record_experience"] = handle_record_experience

    tool_definitions.append({
        "name": "ku_record_gap",
        "description": "记录一个能力/数据缺口（gap）。运行中发现缺什么就落库，便于后续补齐",
        "inputSchema": {
            "type": "object",
            "properties": {
                "topic": {"type": "string", "description": "主题"},
                "context": {"type": "string", "description": "上下文"},
                "missing": {"type": "string", "description": "缺什么（数据/代码/记忆）"},
                "next_action": {"type": "string", "description": "下一步动作"},
                "tags": {"type": "string", "description": "逗号分隔标签"},
            },
            "required": ["topic", "missing"],
        },
    })

    def handle_record_gap(arguments):
        return call_c_vm_memory_thought("gap_record", [
            arguments.get("topic"),
            arguments.get("context"),
            arguments.get("missing"),
            arguments.get("next_action"),
            arguments.get("tags"),
        ])

    tool_handlers["ku_record_gap"] = handle_record_gap

    tool_definitions.append({
        "name": "ku_list_gaps",
        "description": "列出未解决的缺口（gap/code_gap/code_data_gap）",
        "inputSchema": {
            "type": "object",
            "properties": {
                "limit": {"type": "integer", "description": "最大返回条数，默认 20"},
            },
        },
    })

    def handle_list_gaps(arguments):
        limit = arguments.get("limit", 20)
        return call_c_vm_memory_thought("gap_list_open", [coerce_arg(limit)])

    tool_handlers["ku_list_gaps"] = handle_list_gaps

    tool_definitions.append({
        "name": "ku_resolve_gap",
        "description": "标记一个缺口已解决",
        "inputSchema": {
            "type": "object",
            "properties": {
                "id": {"type": "string", "description": "缺口 id"},
                "note": {"type": "string", "description": "解决说明"},
            },
            "required": ["id"],
        },
    })

    def handle_resolve_gap(arguments):
        return call_c_vm_memory_thought("gap_resolve", [arguments.get("id"), arguments.get("note")])

    tool_handlers["ku_resolve_gap"] = handle_resolve_gap

    tool_definitions.append({
        "name": "ku_search_experience",
        "description": "搜索已记录的运行时经验（topic/context/missing/input 模糊匹配）",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {"type": "string", "description": "搜索词"},
                "kind": {"type": "string", "description": "限定类别，可空"},
                "limit": {"type": "integer", "description": "最大返回条数，默认 20"},
            },
            "required": ["query"],
        },
    })

    def handle_search_experience(arguments):
        return call_c_vm_memory_thought("experience_search", [
            arguments.get("query"),
            arguments.get("kind"),
            coerce_arg(arguments.get("limit", 20)),
        ])

    tool_handlers["ku_search_experience"] = handle_search_experience

    tool_definitions.append({
        "name": "ku_record_dataset",
        "description": "登记一个数据集（位置/schema/用途），作为运行时数据记忆",
        "inputSchema": {
            "type": "object",
            "properties": {
                "topic": {"type": "string", "description": "数据集名称"},
                "description": {"type": "string", "description": "用途说明"},
                "location": {"type": "string", "description": "路径/位置"},
                "schema_json": {"type": "string", "description": "schema（JSON 字符串）"},
                "tags": {"type": "string", "description": "逗号分隔标签"},
            },
            "required": ["topic"],
        },
    })

    def handle_record_dataset(arguments):
        return call_c_vm_memory_thought("dataset_record", [
            arguments.get("topic"),
            arguments.get("description"),
            arguments.get("location"),
            arguments.get("schema_json"),
            arguments.get("tags"),
        ])

    tool_handlers["ku_record_dataset"] = handle_record_dataset

    tool_definitions.append({
        "name": "ku_record_data_memory",
        "description": "记录一条结构化数据记忆（key + JSON 值），作为长期事实",
        "inputSchema": {
            "type": "object",
            "properties": {
                "topic": {"type": "string", "description": "主题"},
                "key": {"type": "string", "description": "键"},
                "value_json": {"type": "string", "description": "值（JSON 字符串）"},
                "tags": {"type": "string", "description": "逗号分隔标签"},
            },
            "required": ["topic", "key"],
        },
    })

    def handle_record_data_memory(arguments):
        return call_c_vm_memory_thought("data_memory_record", [
            arguments.get("topic"),
            arguments.get("key"),
            arguments.get("value_json"),
            arguments.get("tags"),
        ])

    tool_handlers["ku_record_data_memory"] = handle_record_data_memory

    if expose_thought_tools:
        # 兼容模式：显式要求时，才把每个 thought 展成 MCP tool。
        with runtime_output_to_stderr():
            thoughts = scan_ku_files(ku_dirs)
        for ku_file, name, params, desc in thoughts:
            tool_def = build_tool_definition(name, params, desc)
            tool_definitions.append(tool_def)
            tool_handlers[tool_def["name"]] = DaoToolHandler(ku_file, name, params)

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
                "serverInfo": {"name": "dao-mcp", "version": "2.0.0"},
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
