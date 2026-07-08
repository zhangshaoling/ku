import json
import os
import subprocess
import sys
import time


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
            "ku_recall_memory_explain",
            "ku_locate_memory",
            "ku_promote_memory",
            "ku_list_memory_promotions",
            "ku_suggest_memory_promotions",
            "ku_call_memory",
            "ku_record_dataset",
            "ku_record_data_memory",
            "ku_graph_from_experience",
            "ku_graph_search_memory",
            "ku_graph_expand_memory",
            "ku_graph_memory_stats",
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


def test_promoted_memories_are_exposed_as_dynamic_mcp_tools(tmp_path):
    env = dict(os.environ)
    env["DAO_DATA_DIR"] = str(tmp_path / "dao_data")
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

        recorded = tool_text(send_request(proc, 2, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "Dao 原生对象",
            "key": "thought-memory",
            "value_json": "{\"equation\":\"memory=code=data=thought\"}",
            "tags": "dao,principle,memory",
        }}))["result"]
        tool_text(send_request(proc, 3, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": recorded["id"],
            "thought_name": "dao_thought_memory_identity",
            "description": "Dao thought-memory identity",
        }}))

        listed = send_request(proc, 4, "tools/list")
        tools = listed["result"]["tools"]
        names = [tool["name"] for tool in tools]
        assert names.count("ku_memory_dao_thought_memory_identity") == 1
        dynamic_tool = next(tool for tool in tools if tool["name"] == "ku_memory_dao_thought_memory_identity")
        assert dynamic_tool["inputSchema"]["properties"]["note"]["type"] == "string"
        assert dynamic_tool["inputSchema"].get("required") is None

        called = tool_text(send_request(proc, 5, "tools/call", {"name": "ku_memory_dao_thought_memory_identity", "arguments": {}}))["result"]
        assert called["thought_name"] == "dao_thought_memory_identity"
        assert called["memory"]["id"] == recorded["id"]
        assert called["memory"]["topic"] == "Dao 原生对象"
    finally:
        if proc.stdin:
            proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_tools_list_reuses_promoted_memory_cache(tmp_path):
    env = dict(os.environ)
    env["DAO_DATA_DIR"] = str(tmp_path / "dao_data")
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

        start = time.perf_counter()
        first = send_request(proc, 2, "tools/list")
        first_elapsed = time.perf_counter() - start

        start = time.perf_counter()
        second = send_request(proc, 3, "tools/list")
        second_elapsed = time.perf_counter() - start

        assert len(first["result"]["tools"]) == len(second["result"]["tools"])
        assert second_elapsed < first_elapsed / 5

        recorded = tool_text(send_request(proc, 4, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "Dao cache refresh test",
            "key": "dynamic-memory-cache",
            "value_json": "{\"ok\":true}",
            "tags": "dao,memory,cache",
        }}))["result"]
        tool_text(send_request(proc, 5, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": recorded["id"],
            "thought_name": "dynamic_memory_cache_refresh",
            "description": "Dynamic memory cache refresh",
        }}))

        listed = send_request(proc, 6, "tools/list")
        names = {tool["name"] for tool in listed["result"]["tools"]}
        assert "ku_memory_dynamic_memory_cache_refresh" in names
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
    env["DAO_DATA_DIR"] = str(tmp_path / "dao_data")
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

        recorded = tool_text(send_request(proc, 4, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "默认路径测试",
            "key": "dynamic-memory",
            "value_json": "{\"ok\":true}",
            "tags": "dao,memory",
        }}))["result"]
        tool_text(send_request(proc, 5, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": recorded["id"],
            "thought_name": "default_path_dynamic_memory",
            "description": "Default path dynamic memory",
        }}))
        listed = send_request(proc, 6, "tools/list")
        names = {tool["name"] for tool in listed["result"]["tools"]}
        assert "ku_memory_default_path_dynamic_memory" in names
        dynamic_result = send_request(
            proc,
            7,
            "tools/call",
            {"name": "ku_memory_default_path_dynamic_memory", "arguments": {}},
        )
        assert tool_text(dynamic_result)["result"]["memory"]["id"] == recorded["id"]
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
