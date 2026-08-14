import io
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest

import dao.c_vm_runtime as c_vm_runtime
from dao.c_vm_runtime import (
    CVMRuntime, DEFAULT_BINARY, DEFAULT_BOOTSTRAP, ROOT, SNAPSHOT_DIR,
    _module_snapshot,
)


@pytest.fixture(scope="module", autouse=True)
def build_c_vm():
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools" / "build_dao_core.py"),
        ],
        cwd=ROOT,
        capture_output=True,
        timeout=120,
    )
    if result.returncode != 0 or not DEFAULT_BINARY.exists():
        stderr = result.stderr.decode("utf-8", "replace") if isinstance(result.stderr, bytes) else str(result.stderr)
        pytest.skip(f"native C VM is not available: {stderr}")


@pytest.fixture()
def runtime():
    instance = CVMRuntime(timeout=60)
    try:
        yield instance
    finally:
        instance.close()


def assert_ok(result):
    assert result.ok, result.error or result.stderr or result.stdout
    return result.value


def test_run_source_frontend_expression(runtime):
    result = runtime.run_source("1 + 2", profile="frontend")
    assert assert_ok(result) == 3
    assert result.command
    assert "dao_core.exe" in Path(result.command[0]).name


def test_run_source_multiline_thought(runtime):
    result = runtime.run_source("思 加一(x) { x + 1 }\n加一(41)", profile="frontend")
    assert assert_ok(result) == 42


def test_profiles_use_valid_module_snapshots(runtime):
    modules = runtime.profile_modules("memory")
    assert modules
    assert all(module.parent == SNAPSHOT_DIR for module in modules)


def test_changed_module_rejects_stale_snapshot(tmp_path):
    source = tmp_path / "math.ku"
    source.write_text((ROOT / "dao" / "std" / "math.ku").read_text(
        encoding="utf-8") + "\n// changed\n", encoding="utf-8")
    assert _module_snapshot(source, DEFAULT_BOOTSTRAP) == source


def test_frontend_worker_reuses_loaded_bootstrap(runtime):
    assert runtime.persistent is False
    assert_ok(runtime.run_source("20 + 22", profile="frontend"))
    assert runtime._workers == {}

    with CVMRuntime(timeout=60, persistent=True) as persistent:
        first = persistent.run_source("20 + 22", profile="frontend")
        second = persistent.run_source("6 * 7", profile="frontend")
        assert assert_ok(first) == 42
        assert assert_ok(second) == 42
        assert first.command == second.command
        assert len(persistent._workers) == 1
        assert next(iter(persistent._workers.values())).request_count == 2


def test_worker_print_does_not_corrupt_protocol():
    with CVMRuntime(timeout=60, persistent=True) as runtime:
        result = runtime.run_source('print("diagnostic")\n40 + 2', profile="frontend")
        assert assert_ok(result) == 42


def test_worker_request_state_is_isolated():
    with CVMRuntime(timeout=60, persistent=True) as runtime:
        assert assert_ok(runtime.run_source("request_only = 42\nrequest_only", profile="frontend")) == 42
        result = runtime.run_source("request_only", profile="frontend")
        assert not result.ok
        assert "NameError" in (result.error or "")


def test_worker_restores_mutated_profile_containers(tmp_path):
    module = tmp_path / "request_state.ku"
    module.write_text("shared_request_values = []\n", encoding="utf-8")
    with CVMRuntime(timeout=60, persistent=True) as runtime:
        first = runtime.run_source(
            "push(shared_request_values, 42)\nlen(shared_request_values)",
            profile="frontend",
            extra_modules=[module],
        )
        second = runtime.run_source(
            "len(shared_request_values)", profile="frontend", extra_modules=[module],
        )
        assert assert_ok(first) == 1
        assert assert_ok(second) == 0


def test_worker_arena_returns_to_baseline_after_success_and_error():
    with CVMRuntime(timeout=60, persistent=True) as runtime:
        for source in ("1", "missing_arena_probe", "2", "missing_arena_probe"):
            runtime.run_source(
                source, profile="frontend", env={"DAO_GC_STATS": "1"},
            )
        worker = next(iter(runtime._workers.values()))
        deadline = time.monotonic() + 1
        while True:
            stats = [
                line for line in worker.stderr.splitlines()
                if line.startswith("[dao-gc-request]")
            ]
            if len(stats) >= 4 or time.monotonic() >= deadline:
                break
            time.sleep(0.01)
        assert len(stats) == 4
        assert len(set(stats)) == 1


def test_worker_pool_is_bounded(tmp_path):
    with CVMRuntime(timeout=60, persistent=True, max_workers=1) as runtime:
        assert_ok(runtime.run_source("1", profile="frontend", data_dir=tmp_path / "one"))
        assert_ok(runtime.run_source("2", profile="frontend", data_dir=tmp_path / "two"))
        assert len(runtime._workers) == 1


def test_worker_pool_reclaims_idle_workers(tmp_path):
    with CVMRuntime(
        timeout=60, persistent=True, max_workers=2, max_worker_idle_seconds=0.01,
    ) as runtime:
        assert_ok(runtime.run_source("1", profile="frontend", data_dir=tmp_path / "one"))
        first_worker = next(iter(runtime._workers.values()))
        first_worker.last_used -= 1
        assert_ok(runtime.run_source("2", profile="frontend", data_dir=tmp_path / "two"))
        assert first_worker._process.poll() is not None
        assert len(runtime._workers) == 1


def test_worker_memory_limit_applies_to_error_responses(monkeypatch):
    monkeypatch.setattr(
        "dao.c_vm_runtime._CVMWorker.private_memory_mb", lambda _worker: 999.0,
    )
    with CVMRuntime(timeout=60, persistent=True, max_private_mb=1) as runtime:
        result = runtime.run_source("missing_request_value", profile="frontend")
        assert not result.ok
        assert runtime._workers == {}


def test_nonpersistent_memory_limit_interrupts_execution(monkeypatch):
    monkeypatch.setattr(
        "dao.c_vm_runtime._process_private_memory_mb", lambda _process: 999.0,
    )
    with CVMRuntime(timeout=60, max_private_mb=1) as runtime:
        result = runtime.run_source("sleep(2)\n1", profile="frontend")
    assert not result.ok
    assert "private memory limit" in (result.error or "")


def test_linux_private_memory_sampler(monkeypatch):
    class Process:
        pid = 123

        @staticmethod
        def poll():
            return None

    monkeypatch.setattr(c_vm_runtime.sys, "platform", "linux")
    monkeypatch.setattr(
        "builtins.open",
        lambda *_args, **_kwargs: io.StringIO(
            "Private_Clean: 1024 kB\nPrivate_Dirty: 2048 kB\n"
        ),
    )
    assert c_vm_runtime._process_private_memory_mb(Process()) == 3


def test_persistent_mode_fails_closed_without_memory_sampler(monkeypatch):
    monkeypatch.setattr(
        "dao.c_vm_runtime._process_private_memory_mb", lambda _process: None,
    )
    with CVMRuntime(timeout=60, persistent=True) as runtime:
        result = runtime.run_source("1", profile="frontend")
        assert not result.ok
        assert "memory measurement is unavailable" in (result.error or "")


def test_worker_memory_limit_interrupts_startup(monkeypatch):
    monkeypatch.setattr(
        "dao.c_vm_runtime._process_private_memory_mb", lambda _process: 999.0,
    )
    with CVMRuntime(timeout=60, persistent=True, max_private_mb=1) as runtime:
        result = runtime.run_source("1", profile="core")
        assert not result.ok
        assert "private memory limit" in (result.error or "")
        assert runtime._workers == {}


def test_worker_concurrent_calls_keep_response_ids_aligned():
    with CVMRuntime(timeout=60, persistent=True, max_worker_requests=100) as runtime:
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(
                lambda value: runtime.run_source(f"{value} + 1", profile="frontend"),
                range(16),
            ))
        assert [assert_ok(result) for result in results] == list(range(1, 17))


def test_different_workers_run_in_parallel(tmp_path):
    with CVMRuntime(timeout=60, persistent=True, max_workers=2) as runtime:
        one = tmp_path / "one"
        two = tmp_path / "two"
        assert_ok(runtime.run_source("1", profile="frontend", data_dir=one))
        assert_ok(runtime.run_source("1", profile="frontend", data_dir=two))
        start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=2) as pool:
            futures = [
                pool.submit(
                    runtime.run_source, "sleep(0.4)\n1",
                    profile="frontend", data_dir=data_dir,
                )
                for data_dir in (one, two)
            ]
            assert [assert_ok(future.result()) for future in futures] == [1, 1]
        assert time.perf_counter() - start < 0.75


def test_close_interrupts_active_worker():
    runtime = CVMRuntime(timeout=60, persistent=True)
    assert_ok(runtime.run_source("1", profile="frontend"))
    with ThreadPoolExecutor(max_workers=1) as pool:
        future = pool.submit(runtime.run_source, "sleep(5)\n1", profile="frontend")
        time.sleep(0.2)
        start = time.perf_counter()
        runtime.close()
        assert time.perf_counter() - start < 1.5
        assert not future.result(timeout=3).ok


def test_worker_memory_stabilizes_after_warmup():
    with CVMRuntime(
        timeout=60, persistent=True, max_worker_requests=4000, max_private_mb=4096,
    ) as runtime:
        start_mb = None
        pid = None
        for index in range(3000):
            assert assert_ok(runtime.run_source("20 + 22", profile="frontend")) == 42
            if index == 999:
                worker = next(iter(runtime._workers.values()))
                pid = worker._process.pid
                start_mb = worker.private_memory_mb()
        worker = next(iter(runtime._workers.values()))
        assert worker._process.pid == pid
        assert worker.request_count == 3000
        end_mb = worker.private_memory_mb()
        if start_mb is not None and end_mb is not None:
            assert end_mb - start_mb < 2


def test_worker_closes_request_owned_sqlite_connections(tmp_path):
    with CVMRuntime(timeout=60, persistent=True, max_worker_requests=200) as runtime:
        for _ in range(100):
            result = runtime.run_source(
                'sqlite_open(dao_data_path("resource.db"))',
                profile="frontend",
                data_dir=tmp_path,
            )
            assert assert_ok(result) == 1


def test_call_thought_core_profile(runtime):
    result = runtime.call_thought("求和", [[1, 2, 3]], params=["列表"], profile="core")
    assert assert_ok(result) == 6


def test_call_thought_preserves_string_args(runtime):
    result = runtime.call_thought("is_numeric", ["12345"], params=["s"], profile="core")
    assert assert_ok(result) is True


def test_call_thought_preserves_utf8_string_args(runtime):
    text = "\u4e2d\u6587\u8bb0\u5fc6"
    result = runtime.call_thought("str", [text], params=["x"], profile="core")
    assert assert_ok(result) == text


def test_memory_profile_experience_roundtrip(runtime, tmp_path):
    data_dir = tmp_path / "dao_data"

    recorded = runtime.call_thought(
        "gap_record",
        ["M4.5", "runtime gateway test", "needs C VM memory", "keep going", "gateway,memory"],
        params=["topic", "context", "missing", "next_action", "tags"],
        profile="memory",
        data_dir=data_dir,
    )
    value = assert_ok(recorded)
    assert value["kind"] == "gap"
    assert value["status"] == "open"
    assert (data_dir / "experience.db").exists()

    listed = runtime.call_thought("gap_list_open", [20], params=["limit"], profile="memory", data_dir=data_dir)
    gaps = assert_ok(listed)
    assert gaps["count"] >= 1
    assert any(row["topic"] == "M4.5" for row in gaps["gaps"])


def test_memory_profile_graph_indexes_and_expands_related_memories(runtime, tmp_path):
    data_dir = tmp_path / "dao_data"

    first = runtime.call_thought(
        "experience_record",
        ["data_memory", "Graph Memory Alpha", "alpha-key", "alpha-value", "", "", "", "dao,graph"],
        params=["kind", "topic", "context", "input", "output", "missing", "next_action", "tags"],
        profile="memory",
        data_dir=data_dir,
    )
    first_value = assert_ok(first)
    assert first_value["graph"]["node"]["memory_id"] == first_value["id"]
    assert first_value["graph"]["auto_edges"] == 0

    second = runtime.call_thought(
        "experience_record",
        ["data_memory", "Graph Memory Beta", "beta-key", "beta-value", "", "", "", "dao,graph"],
        params=["kind", "topic", "context", "input", "output", "missing", "next_action", "tags"],
        profile="memory",
        data_dir=data_dir,
    )
    second_value = assert_ok(second)
    assert second_value["graph"]["node"]["memory_id"] == second_value["id"]
    assert second_value["graph"]["auto_edges"] >= 1

    first_node = runtime.call_thought(
        "memory_graph_from_experience",
        [first_value["id"]],
        params=["experience_id"],
        profile="memory",
        data_dir=data_dir,
    )
    assert_ok(first_node)

    second_node = runtime.call_thought(
        "memory_graph_from_experience",
        [second_value["id"]],
        params=["experience_id"],
        profile="memory",
        data_dir=data_dir,
    )
    second_node_value = assert_ok(second_node)
    assert second_node_value["auto_edges"] >= 1

    expanded = runtime.call_thought(
        "memory_graph_expand",
        ["Graph Memory Beta", 5],
        params=["query", "limit"],
        profile="memory",
        data_dir=data_dir,
    )
    expanded_value = assert_ok(expanded)
    assert expanded_value["count"] == 1
    neighbor_memory_ids = {
        neighbor["memory_id"]
        for item in expanded_value["expanded"]
        for neighbor in item["neighbors"]
    }
    assert first_value["id"] in neighbor_memory_ids

    stats = runtime.call_thought("memory_graph_stats", [], profile="memory", data_dir=data_dir)
    stats_value = assert_ok(stats)
    assert stats_value["nodes"] == 2
    assert stats_value["edges"] >= 2
    assert stats_value["keywords"] >= 4


def test_memory_profile_preserves_utf8_through_sqlite(runtime, tmp_path):
    data_dir = tmp_path / "dao_data"
    topic = "\u5ba1\u8ba1"

    direct = runtime.eval_code(
        (
            'db = sqlite_open(dao_data_path("utf8.db"))\n'
            'sqlite_exec(db, "CREATE TABLE IF NOT EXISTS probe (x TEXT)", [])\n'
            'sqlite_exec(db, "DELETE FROM probe", [])\n'
            f'sqlite_exec(db, "INSERT INTO probe VALUES (?)", ["{topic}"])\n'
            'rows = sqlite_query(db, "SELECT x FROM probe", [])\n'
            "sqlite_close(db)\n"
            'rows[0]["x"]\n'
        ),
        profile="frontend",
        data_dir=data_dir,
    )
    assert assert_ok(direct) == topic

    recorded = runtime.call_thought(
        "gap_record",
        [topic, "\u8fd0\u884c\u65f6\u89c2\u5bdf", "\u4e2d\u6587\u7ecf\u9a8c\u8bb0\u5fc6", "\u4fdd\u6301 UTF-8 \u5f80\u8fd4", "utf8,sqlite"],
        params=["topic", "context", "missing", "next_action", "tags"],
        profile="memory",
        data_dir=data_dir,
    )
    assert_ok(recorded)

    listed = runtime.call_thought("gap_list_open", [20], params=["limit"], profile="memory", data_dir=data_dir)
    gaps = assert_ok(listed)
    assert any(row["topic"] == topic for row in gaps["gaps"])


def test_tiandao_mcp_profile_runs_meta_rule_scheduler(runtime, tmp_path):
    data_dir = tmp_path / "dao_data"

    first = runtime.call_thought("天道", ["道记忆", {}], profile="tiandao_mcp", data_dir=data_dir)
    value = assert_ok(first)
    assert value["action"] == "reason_and_learn"
    assert value["rule"] == "02+03"
    assert value["result"]["method"] == "tiandao_mcp_fast"
    assert (data_dir / "memory.db").exists()

    stats = runtime.call_thought("天道统计", [], profile="tiandao_mcp", data_dir=data_dir)
    stats_value = assert_ok(stats)
    assert stats_value["memory"]["experience"] == 1


def test_runtime_error_shape_for_unknown_thought(runtime):
    result = runtime.call_thought("不存在的思", [], params=[], profile="frontend")
    assert not result.ok
    assert result.error
    assert "NameError" in result.error
