import json
import os
import sqlite3
import subprocess
import sys

import pytest


def _spawn(data_dir):
    env = dict(os.environ)
    env["DAO_DATA_DIR"] = str(data_dir)
    build = subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            os.path.join(os.getcwd(), "tools", "build_dao_core.ps1"),
        ],
        capture_output=True,
        timeout=120,
    )
    if build.returncode != 0:
        stderr = build.stderr.decode("utf-8", "replace") if isinstance(build.stderr, bytes) else build.stderr
        if "No native C compiler found" in stderr:
            pytest.skip("native C compiler is required for dao_core integration tests")
        raise AssertionError(stderr)
    return subprocess.Popen(
        [sys.executable, "-m", "dao.mcp_server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
    )


def _req(proc, req_id, method, params=None):
    msg = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        msg["params"] = params
    proc.stdin.write(json.dumps(msg, ensure_ascii=False) + "\n")
    proc.stdin.flush()
    line = proc.stdout.readline()
    assert line, proc.stderr.read()
    return json.loads(line)


def _notify(proc, method):
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
    proc.stdin.flush()


def _text(response):
    return json.loads(response["result"]["content"][0]["text"])["result"]


def _init(proc):
    _req(
        proc,
        1,
        "initialize",
        {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "pytest", "version": "0"},
        },
    )
    _notify(proc, "notifications/initialized")


def _close(proc):
    if proc.stdin:
        proc.stdin.close()
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()


def test_experience_memory_roundtrip_and_persistence(tmp_path):
    data_dir = tmp_path / "dao_data"

    proc = _spawn(data_dir)
    try:
        _init(proc)

        recorded = _req(
            proc,
            2,
            "tools/call",
            {
                "name": "ku_record_gap",
                "arguments": {
                    "topic": "MCP 工具面",
                    "context": "运行时观察",
                    "missing": "thought 调用日志数据集",
                    "next_action": "采集 100 条调用样本",
                    "tags": "mcp,dataset",
                },
            },
        )
        gap = _text(recorded)
        assert gap["kind"] == "gap"
        assert gap["status"] == "open"
        gap_id = gap["id"]

        listed = _text(_req(proc, 3, "tools/call", {"name": "ku_list_gaps", "arguments": {"limit": 10}}))
        assert listed["count"] == 1
        assert listed["gaps"][0]["topic"] == "MCP 工具面"
        assert listed["gaps"][0]["tags"] == ["mcp", "dataset"]

        _text(_req(proc, 4, "tools/call", {"name": "ku_record_dataset", "arguments": {
            "topic": "调用日志", "description": "thought 调用样本", "location": "data/calls.jsonl",
            "schema_json": "{}", "tags": "dataset",
        }}))
        _text(_req(proc, 5, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "用户偏好", "key": "少工具高能力", "value_json": "{\"why\": \"省上下文\"}", "tags": "feedback",
        }}))

        searched = _text(_req(proc, 6, "tools/call", {"name": "ku_search_experience", "arguments": {"query": "工具面"}}))
        assert searched["count"] >= 1

        resolved = _text(_req(proc, 7, "tools/call", {"name": "ku_resolve_gap", "arguments": {
            "id": gap_id, "note": "已补充 experience.ku",
        }}))
        assert resolved["status"] == "resolved"

        after = _text(_req(proc, 8, "tools/call", {"name": "ku_list_gaps", "arguments": {"limit": 10}}))
        assert after["count"] == 0
    finally:
        _close(proc)

    # 新进程读取同一 DAO_DATA_DIR，验证数据落库持久化。
    proc2 = _spawn(data_dir)
    try:
        _init(proc2)
        persisted = _text(_req(proc2, 2, "tools/call", {"name": "ku_search_experience", "arguments": {
            "query": "调用日志", "kind": "dataset",
        }}))
        assert persisted["count"] >= 1
        assert persisted["results"][0]["topic"] == "调用日志"
    finally:
        _close(proc2)


def test_memory_recall_uses_fts_and_preserves_utf8(tmp_path):
    data_dir = tmp_path / "dao_data"

    proc = _spawn(data_dir)
    try:
        _init(proc)

        _text(_req(proc, 2, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "AGI 长期记忆",
            "key": "项目方向",
            "value_json": "{\"rule\":\"代码等于记忆等于数据\"}",
            "tags": "agi,memory,dao",
        }}))
        _text(_req(proc, 3, "tools/call", {"name": "ku_record_gap", "arguments": {
            "topic": "临时缺口",
            "context": "不会被 data_memory 召回",
            "missing": "无",
            "next_action": "无",
            "tags": "gap",
        }}))

        recalled = _text(_req(proc, 4, "tools/call", {"name": "ku_recall_memory", "arguments": {
            "query": "长期记忆",
            "kind": "data_memory",
            "limit": 5,
        }}))
        assert recalled["count"] == 1
        assert recalled["query"] == "长期记忆"
        assert recalled["kind"] == "data_memory"
        assert recalled["memories"][0]["topic"] == "AGI 长期记忆"
        assert recalled["memories"][0]["context"] == "项目方向"
        assert recalled["memories"][0]["tags"] == ["agi", "memory", "dao"]

        explained = _text(_req(proc, 5, "tools/call", {"name": "ku_recall_memory_explain", "arguments": {
            "query": "长期记忆",
            "kind": "data_memory",
            "limit": 5,
        }}))
        assert explained["count"] == 1
        assert explained["explanations"][0]["memory_id"] == recalled["memories"][0]["id"]
        assert "topic match" in explained["explanations"][0]["reason"]

        recent = _text(_req(proc, 6, "tools/call", {"name": "ku_recall_memory", "arguments": {
            "query": "",
            "limit": 10,
        }}))
        assert recent["count"] >= 2
    finally:
        _close(proc)

    with sqlite3.connect(data_dir / "experience.db") as conn:
        fts_table = conn.execute(
            "SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'experience_fts'"
        ).fetchone()
    assert fts_table is not None


def test_memory_locate_returns_stable_address_and_promoted_route(tmp_path):
    data_dir = tmp_path / "dao_data"

    proc = _spawn(data_dir)
    try:
        _init(proc)

        recorded = _text(_req(proc, 2, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "Executable Memory Route",
            "key": "data-code-memory-tool",
            "value_json": "{\"rule\":\"data equals code equals memory equals tool\"}",
            "tags": "dao,memory,capability",
        }}))

        located = _text(_req(proc, 3, "tools/call", {"name": "ku_locate_memory", "arguments": {
            "query": "Executable Memory Route",
            "kind": "data_memory",
            "limit": 5,
        }}))
        assert located["count"] == 1
        locator = located["locators"][0]
        assert locator["id"] == recorded["id"]
        assert locator["address"] == f"dao://experience/{recorded['id']}"
        assert locator["route"]["store"] == "experience"
        assert locator["route"]["table"] == "experience"
        assert locator["route"]["id"] == recorded["id"]
        assert locator["promoted"] is False
        assert locator["tool_name"] == ""

        _text(_req(proc, 4, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": recorded["id"],
            "thought_name": "executable_memory_route",
            "description": "Executable memory route locator",
        }}))

        promoted_located = _text(_req(proc, 5, "tools/call", {"name": "ku_locate_memory", "arguments": {
            "query": "Executable Memory Route",
            "kind": "data_memory",
            "limit": 5,
        }}))
        promoted_locator = promoted_located["locators"][0]
        assert promoted_locator["promoted"] is True
        assert promoted_locator["thought_name"] == "executable_memory_route"
        assert promoted_locator["tool_name"] == "ku_memory_executable_memory_route"
    finally:
        _close(proc)


def test_memory_promotion_makes_memory_callable(tmp_path):
    data_dir = tmp_path / "dao_data"

    proc = _spawn(data_dir)
    try:
        _init(proc)

        recorded = _text(_req(proc, 2, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "Dao 发展原则",
            "key": "语义权威",
            "value_json": "{\"rule\":\"Python 不能悄悄成为语义权威\"}",
            "tags": "dao,principle,memory",
        }}))

        promoted = _text(_req(proc, 3, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": recorded["id"],
            "thought_name": "dao_semantic_authority",
            "description": "Dao semantic authority principle",
        }}))
        assert promoted["experience_id"] == recorded["id"]
        assert promoted["thought_name"] == "dao_semantic_authority"
        assert promoted["tool_name"] == "ku_memory_dao_semantic_authority"
        assert promoted["policy"] == "promotion_policy_v1"
        assert "data_memory" in promoted["reason"]
        assert promoted["memory"]["topic"] == "Dao 发展原则"

        called = _text(_req(proc, 4, "tools/call", {"name": "ku_call_memory", "arguments": {
            "thought_name": "dao_semantic_authority",
        }}))
        assert called["thought_name"] == "dao_semantic_authority"
        assert called["memory"]["id"] == recorded["id"]
        assert called["memory"]["context"] == "语义权威"
        assert called["memory"]["tags"] == ["dao", "principle", "memory"]

        listed_promotions = _text(_req(proc, 5, "tools/call", {"name": "ku_list_memory_promotions", "arguments": {}}))
        assert listed_promotions["count"] == 1
        assert listed_promotions["promotions"][0]["tool_name"] == "ku_memory_dao_semantic_authority"

        listed_tools = _req(proc, 6, "tools/list")
        tool_names = [tool["name"] for tool in listed_tools["result"]["tools"]]
        assert tool_names.count("ku_memory_dao_semantic_authority") == 1

        dynamic_called = _text(_req(proc, 7, "tools/call", {"name": "ku_memory_dao_semantic_authority", "arguments": {
            "note": "测试动态 thought-memory 工具调用",
        }}))
        assert dynamic_called["thought_name"] == "dao_semantic_authority"
        assert dynamic_called["memory"]["id"] == recorded["id"]

        suggestions = _text(_req(proc, 8, "tools/call", {"name": "ku_suggest_memory_promotions", "arguments": {
            "query": "Dao",
            "kind": "data_memory",
            "limit": 5,
        }}))
        assert suggestions["policy"] == "promotion_policy_v1"
        assert suggestions["count"] >= 1
        assert suggestions["candidates"][0]["recommended"] is True

        replacement = _text(_req(proc, 9, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "Dao 发展原则 v2",
            "key": "语义权威",
            "value_json": "{\"rule\":\"默认执行必须由 C VM 负责\"}",
            "tags": "dao,principle,memory,longterm",
        }}))
        updated = _text(_req(proc, 10, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": replacement["id"],
            "thought_name": "dao_semantic_authority",
            "description": "Updated Dao semantic authority principle",
        }}))
        assert updated["experience_id"] == replacement["id"]

        listed_tools_after_update = _req(proc, 11, "tools/list")
        tool_names_after_update = [tool["name"] for tool in listed_tools_after_update["result"]["tools"]]
        assert tool_names_after_update.count("ku_memory_dao_semantic_authority") == 1

        updated_dynamic_called = _text(_req(proc, 12, "tools/call", {"name": "ku_memory_dao_semantic_authority", "arguments": {}}))
        assert updated_dynamic_called["memory"]["id"] == replacement["id"]
    finally:
        _close(proc)

    with sqlite3.connect(data_dir / "experience.db") as conn:
        conn.row_factory = sqlite3.Row
        promotion = conn.execute("SELECT * FROM memory_promotion").fetchone()
        promotion_meta = conn.execute("SELECT * FROM memory_promotion_meta WHERE promotion_id = ?", (promotion["id"],)).fetchone()
        link = conn.execute("SELECT * FROM experience_link WHERE thought_name = ?", ("dao_semantic_authority",)).fetchone()
        call_observation = conn.execute("SELECT * FROM experience WHERE kind = 'observation' AND topic = ?", ("memory_call:dao_semantic_authority",)).fetchone()
    assert promotion["experience_id"] == replacement["id"]
    assert promotion["thought_name"] == "dao_semantic_authority"
    assert promotion_meta["reason"]
    assert promotion_meta["policy"] == "promotion_policy_v1"
    assert link["experience_id"] in {recorded["id"], replacement["id"]}
    assert call_observation["context"] == "测试动态 thought-memory 工具调用"


def test_promoted_memory_missing_target_returns_structured_error(tmp_path):
    data_dir = tmp_path / "dao_data"
    proc = _spawn(data_dir)
    try:
        _init(proc)
        recorded = _text(_req(proc, 2, "tools/call", {"name": "ku_record_data_memory", "arguments": {
            "topic": "可删除记忆",
            "key": "target",
            "value_json": "{\"ok\":true}",
            "tags": "dao,memory",
        }}))
        _text(_req(proc, 3, "tools/call", {"name": "ku_promote_memory", "arguments": {
            "experience_id": recorded["id"],
            "thought_name": "deleted_target_memory",
            "description": "Will point to a deleted target",
        }}))
    finally:
        _close(proc)

    with sqlite3.connect(data_dir / "experience.db") as conn:
        conn.execute("DELETE FROM experience WHERE id = ?", (recorded["id"],))
        conn.commit()

    proc2 = _spawn(data_dir)
    try:
        _init(proc2)
        called = _text(_req(proc2, 2, "tools/call", {"name": "ku_call_memory", "arguments": {
            "thought_name": "deleted_target_memory",
        }}))
        assert called["error"] == "promoted memory target missing"
        assert called["thought_name"] == "deleted_target_memory"
    finally:
        _close(proc2)


def test_experience_db_lands_in_dao_data_dir(tmp_path):
    data_dir = tmp_path / "dao_data"
    proc = _spawn(data_dir)
    try:
        _init(proc)
        _text(_req(proc, 2, "tools/call", {"name": "ku_record_experience", "arguments": {
            "kind": "observation", "topic": "数据目录隔离", "context": "测试",
        }}))
    finally:
        _close(proc)

    assert (data_dir / "experience.db").exists()


def test_gap_to_task_queues_task_in_shared_dao_data_dir(tmp_path):
    data_dir = tmp_path / "dao_data"
    proc = _spawn(data_dir)
    try:
        _init(proc)

        gap = _text(_req(proc, 2, "tools/call", {"name": "ku_record_gap", "arguments": {
            "topic": "gap_to_task 桥接",
            "context": "测试",
            "missing": "任务队列落库验证",
            "next_action": "写入 task_queue",
            "tags": "task",
        }}))

        queued = _text(_req(proc, 3, "tools/call", {"name": "ku_call", "arguments": {
            "name": "gap_to_task",
            "arguments": {"gap_id": gap["id"], "task_type": "reasoning", "priority": 1},
        }}))
        assert queued["status"] == "queued"
        assert queued["gap_id"] == gap["id"]
        assert queued["task_id"]
    finally:
        _close(proc)

    task_db = data_dir / "task_queue.db"
    assert task_db.exists()
    assert not (data_dir / ".hermes").exists()

    with sqlite3.connect(task_db) as conn:
        conn.row_factory = sqlite3.Row
        task = conn.execute("SELECT * FROM task_queue").fetchone()
    assert task["id"] == queued["task_id"]
    assert task["type"] == "reasoning"
    assert task["priority"] == 1
    assert task["prompt"] == "gap_to_task 桥接 | 缺: 任务队列落库验证 | 下一步: 写入 task_queue"

    with sqlite3.connect(data_dir / "experience.db") as conn:
        conn.row_factory = sqlite3.Row
        link = conn.execute("SELECT * FROM experience_link").fetchone()
    assert link["experience_id"] == gap["id"]
    assert link["task_id"] == queued["task_id"]
