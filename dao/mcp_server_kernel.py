import json
import os
import subprocess
import sys
from pathlib import Path

# -- encoding --
if sys.platform == "win32":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# -- paths --
BINDINGS_DIR = Path(__file__).resolve().parent.parent / "bindings" / "python"
sys.path.insert(0, str(BINDINGS_DIR))

from dao_kernel import Thought, Runtime, DaoValue, DAO_VALUE_I64, fnv1a

DAO_KU = Path(__file__).resolve().parent.parent / "kernel" / "out" / "cmake" / "bin" / "dao-ku.exe"
KERNEL_DLL = Path(__file__).resolve().parent.parent / "kernel" / "out" / "cmake" / "bin" / "libdao_kernel.dll"


def rpc_result(req_id, result):
    msg = json.dumps({"jsonrpc": "2.0", "id": req_id, "result": result}, ensure_ascii=True)
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


def rpc_error(req_id, code, message):
    msg = json.dumps({"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}, ensure_ascii=True)
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


def compile_ku(ku_path: Path, dao_path: Path) -> bool:
    """Compile .ku to .dao using dao-ku compiler."""
    try:
        result = subprocess.run(
            [str(DAO_KU), str(ku_path), str(dao_path)],
            capture_output=True, timeout=30,
        )
        return result.returncode == 0
    except Exception:
        return False


def parse_thought_signature(source: str):
    """Parse .ku source to extract thought name, params, and imports."""
    name = None
    params = []
    imports = []
    for line in source.splitlines():
        line = line.strip()
        if line.startswith("thought "):
            parts = line[8:].split("(")
            if name is None:
                name = parts[0].strip()
                if len(parts) > 1:
                    params = [p.strip() for p in parts[1].rstrip(") {").split(",") if p.strip()]
        elif line.startswith("import "):
            imp = line[7:].strip()
            if "(" in imp:
                fn_name = imp.split("(")[0].strip()
                arity = int(imp.split("(")[1].split(")")[0])
                imports.append((fn_name, arity))
    return name, params, imports


def make_host_function_stub(name: str, arity: int):
    """Create a stub host function that returns 0."""
    def stub(args, count):
        return DaoValue(DAO_VALUE_I64, 0, 0)
    stub.__name__ = name
    return stub


class KernelTool:
    """A single Ku thought exposed as an MCP tool."""
    def __init__(self, name: str, params: list[str], doc: str,
                 thought: Thought, imports: list[tuple[str, int]]):
        self.name = name
        self.params = params
        self.doc = doc
        self.thought = thought
        self.imports = imports

    def mcp_schema(self):
        properties = {}
        for p in self.params:
            properties[p] = {"type": "integer", "description": f"Parameter {p}"}
        return {
            "name": self.name,
            "description": self.doc or f"Ku thought: {self.name}",
            "inputSchema": {
                "type": "object",
                "properties": properties,
                "required": self.params,
            },
        }

    def execute(self, runtime: Runtime, arguments: dict) -> dict:
        """Execute the thought with given arguments."""
        for fn_name, arity in self.imports:
            runtime.register_host_function(fnv1a(fn_name), arity, make_host_function_stub(fn_name, arity))

        module = runtime.load(self.thought.module_bytes)
        try:
            args = [arguments.get(p, 0) for p in self.params]
            result = module.call_i64(fnv1a("main"), *args)
            return {"result": result}
        finally:
            module.close()


def scan_and_compile(ku_dirs: list[Path]) -> list[KernelTool]:
    """Scan .ku files, compile to .dao, create KernelTools."""
    tools = []
    for ku_dir in ku_dirs:
        if not ku_dir.is_dir():
            continue
        for ku_file in sorted(ku_dir.glob("*.ku")):
            dao_file = ku_file.with_suffix(".dao")
            if not dao_file.exists() or dao_file.stat().st_mtime < ku_file.stat().st_mtime:
                if not compile_ku(ku_file, dao_file):
                    continue
            if not dao_file.exists():
                continue
            source = ku_file.read_text(encoding="utf-8")
            name, params, imports = parse_thought_signature(source)
            if name is None:
                continue
            module_bytes = dao_file.read_bytes()
            tool_name = ku_file.stem
            thought = Thought(name, params, module_bytes, doc=f"Ku thought from {ku_file.name}")
            tools.append(KernelTool(tool_name, params, "", thought, imports))
    return tools


def main():
    raw_args = sys.argv[1:]
    ku_dirs = [Path(d) for d in raw_args] if raw_args else [Path(__file__).resolve().parent.parent / "demos"]

    tools = scan_and_compile(ku_dirs)
    tool_map = {t.name: t for t in tools}

    runtime = Runtime(KERNEL_DLL)

    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                req = json.loads(line)
            except json.JSONDecodeError:
                continue

            req_id = req.get("id")
            method = req.get("method", "")
            params = req.get("params", {})

            if method == "tools/list":
                tool_list = [t.mcp_schema() for t in tools]
                rpc_result(req_id, {"tools": tool_list})

            elif method == "tools/call":
                tool_name = params.get("name", "")
                arguments = params.get("arguments", {})
                tool = tool_map.get(tool_name)
                if tool is None:
                    rpc_error(req_id, -32601, f"Tool '{tool_name}' not found")
                else:
                    try:
                        result = tool.execute(runtime, arguments)
                        rpc_result(req_id, {"content": [{"type": "text", "text": json.dumps(result, ensure_ascii=True)}]})
                    except Exception as e:
                        rpc_error(req_id, -32603, str(e))

            elif method == "initialize":
                rpc_result(req_id, {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "ku-kernel-mcp", "version": "0.1.0"},
                })

            elif req_id is not None:
                rpc_error(req_id, -32601, f"Method '{method}' not supported")
    finally:
        runtime.close()


if __name__ == "__main__":
    main()
