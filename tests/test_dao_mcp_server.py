import json
import os
import subprocess
import sys


def send_request(proc, req_id, method, params=None):
    msg = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        msg["params"] = params
    proc.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline()
    assert line, proc.stderr.read()
    return json.loads(line)


def send_notification(proc, method):
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
    proc.stdin.flush()


def tool_text(response):
    return json.loads(response["result"]["content"][0]["text"])


def test_dao_mcp_server_lists_and_calls_tools():
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        init = send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        assert init["result"]["serverInfo"]["name"] == "dao-mcp"
        send_notification(proc, "notifications/initialized")

        listed = send_request(proc, 2, "tools/list")
        tools = listed["result"]["tools"]
        names = {tool["name"] for tool in tools}
        # 默认网关工具：少量 schema，不展开 344 个 thought。
        assert {"ku_eval", "ku_call", "ku_list_thoughts"}.issubset(names)
        # 经验记忆网关工具也默认可用。
        assert {
            "ku_record_experience",
            "ku_record_gap",
            "ku_list_gaps",
            "ku_search_experience",
            "ku_recall_memory",
            "ku_record_dataset",
            "ku_record_data_memory",
        }.issubset(names)
        # 不应默认把每个 thought 展成单独工具。
        assert "ku_斐波那契" not in names
        assert len(names) < 30

        eval_result = send_request(
            proc,
            3,
            "tools/call",
            {"name": "ku_eval", "arguments": {"code": "1 + 2"}},
        )
        assert tool_text(eval_result) == {"result": 3}

        multiline_eval_result = send_request(
            proc,
            4,
            "tools/call",
            {"name": "ku_eval", "arguments": {"code": "思 加一(x) { x + 1 }\n加一(41)"}},
        )
        assert tool_text(multiline_eval_result) == {"result": 42}

        sum_result = send_request(
            proc,
            5,
            "tools/call",
            {"name": "ku_call", "arguments": {"name": "求和", "arguments": {"列表": [1, 2, 3]}}},
        )
        assert tool_text(sum_result) == {"result": 6}

        fib_result = send_request(
            proc,
            6,
            "tools/call",
            {"name": "ku_call", "arguments": {"name": "斐波那契", "arguments": {"n": 10}}},
        )
        assert tool_text(fib_result) == {"result": 55}

        join_result = send_request(
            proc,
            7,
            "tools/call",
            {"name": "ku_call", "arguments": {"name": "连接", "arguments": {"项": ["天", "书"], "分隔符": ""}}},
        )
        assert tool_text(join_result) == {"result": "天书"}
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_dao_mcp_server_exposes_golden_path_as_default_tool():
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        listed = send_request(proc, 2, "tools/list")
        names = {tool["name"] for tool in listed["result"]["tools"]}
        assert "ku_golden_path" in names

        result = send_request(proc, 3, "tools/call", {"name": "ku_golden_path", "arguments": {}})
        assert tool_text(result) == {
            "result": {
                "thought": "加一",
                "code": "thought",
                "memory": "thought = code = memory",
                "result": 42,
            }
        }
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_dao_mcp_server_can_expose_thought_tools_explicitly():
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server", "--expose-thought-tools"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        listed = send_request(proc, 2, "tools/list")
        names = {tool["name"] for tool in listed["result"]["tools"]}
        assert {"ku_eval", "ku_call", "ku_list_thoughts"}.issubset(names)
        assert "ku_斐波那契" in names
        assert len(names) > 3

        fib_result = send_request(
            proc,
            3,
            "tools/call",
            {"name": "ku_斐波那契", "arguments": {"n": 10}},
        )
        assert tool_text(fib_result) == {"result": 55}
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_ku_call_preserves_string_arguments():
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        result = send_request(
            proc,
            2,
            "tools/call",
            {"name": "ku_call", "arguments": {"name": "is_numeric", "arguments": {"s": "12345"}}},
        )
        assert tool_text(result) == {"result": True}
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_default_mcp_execution_does_not_call_python_thought(tmp_path):
    sitecustomize = tmp_path / "sitecustomize.py"
    sitecustomize.write_text(
        """
import dao.runtime


def fail_if_called(self, args=None, env=None):
    raise AssertionError("default MCP execution called Python Thought.call")


dao.runtime.Thought.call = fail_if_called
""".lstrip(),
        encoding="utf-8",
    )

    env = dict(os.environ)
    env.pop("DAO_MCP_ALLOW_PYTHON_FALLBACK", None)
    existing_pythonpath = env.get("PYTHONPATH")
    env["PYTHONPATH"] = (
        str(tmp_path)
        if not existing_pythonpath
        else str(tmp_path) + os.pathsep + existing_pythonpath
    )

    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        eval_result = send_request(
            proc,
            2,
            "tools/call",
            {"name": "ku_eval", "arguments": {"code": "1 + 2"}},
        )
        assert tool_text(eval_result) == {"result": 3}

        call_result = send_request(
            proc,
            3,
            "tools/call",
            {"name": "ku_call", "arguments": {"name": "is_numeric", "arguments": {"s": "12345"}}},
        )
        assert tool_text(call_result) == {"result": True}
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_ku_eval_surfaces_sqlite_errors_as_tool_errors():
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        result = send_request(
            proc,
            2,
            "tools/call",
            {"name": "ku_eval", "arguments": {
                "code": "conn = sqlite_open(dao_data_path(\"bad.db\"))\nsqlite_exec(conn, \"INSERT INTO missing_table (x) VALUES (1)\", [])"
            }},
        )
        assert result["error"]["code"] == -32603
        assert "no such table: missing_table" in result["error"]["message"]
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_ku_eval_requires_explicit_python_fallback_when_c_vm_missing(tmp_path):
    env = dict(os.environ)
    env["DAO_CVM_BINARY"] = str(tmp_path / "missing_dao_core.exe")
    env.pop("DAO_MCP_ALLOW_PYTHON_FALLBACK", None)
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        result = send_request(
            proc,
            2,
            "tools/call",
            {"name": "ku_eval", "arguments": {"code": "1 + 2"}},
        )
        assert result["error"]["code"] == -32603
        assert "C VM binary not found" in result["error"]["message"]
        assert "DAO_MCP_ALLOW_PYTHON_FALLBACK=1" in result["error"]["message"]
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_ku_eval_python_fallback_is_debug_opt_in(tmp_path):
    env = dict(os.environ)
    env["DAO_CVM_BINARY"] = str(tmp_path / "missing_dao_core.exe")
    env["DAO_MCP_ALLOW_PYTHON_FALLBACK"] = "1"
    proc = subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )
    try:
        send_request(
            proc,
            1,
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "pytest", "version": "0"},
            },
        )
        send_notification(proc, "notifications/initialized")

        result = send_request(
            proc,
            2,
            "tools/call",
            {"name": "ku_eval", "arguments": {"code": "1 + 2"}},
        )
        assert tool_text(result) == {"result": 3}
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
