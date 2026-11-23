"""Validate an installed SDK or release ZIP from an unrelated working directory."""
import hashlib
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def verify(prefix, work):
    suffix = ".exe" if os.name == "nt" else ""
    compiler = prefix / "bin" / ("dao-ku" + suffix)
    module_cli = prefix / "bin" / ("dao-module" + suffix)
    environment = dict(os.environ)
    environment.pop("LD_LIBRARY_PATH", None)
    environment.pop("DYLD_LIBRARY_PATH", None)
    system_paths = ["/usr/bin", "/bin"]
    if os.name == "nt":
        system_root = Path(os.environ["SystemRoot"])
        system_paths = [str(system_root / "System32"), str(system_root)]
    environment["PATH"] = os.pathsep.join([str(prefix / "bin"), *system_paths])
    environment["PYTHONPATH"] = str(prefix / "bindings" / "python")
    environment["PYTHONNOUSERSITE"] = "1"
    environment["DAO_KERNEL_DLL_DIRS"] = ""

    def run(command):
        result = subprocess.run(
            [str(item) for item in command], cwd=work, env=environment,
            capture_output=True, text=True, encoding="utf-8", timeout=30,
        )
        if result.returncode:
            raise RuntimeError(f"{command[0]} failed\n{result.stdout}\n{result.stderr}")
        return result.stdout.strip()

    source = work / "answer.ku"
    source.write_text("思 main() { 返 42 }\n", encoding="utf-8")
    run([compiler, "--check", source])
    run([compiler, "--fmt", source])
    run([compiler, source, work / "answer.dao"])
    store = work / "store"
    run([compiler, "--store", store, "--identity", "ku:agent/release", "1.0.0", source])
    run([module_cli, "verify", store, "ku:agent/release", "1.0.0"])
    assert run([module_cli, "run", store, "ku:agent/release", "1.0.0", "main"]) == "42"

    record_source = work / "record_store.ku"
    record_source.write_text('型 点 { 横: 整数 }\n思 main() { 值 = 点 { 横: 42 }; 返 值.横 }', encoding="utf-8")
    record_store = work / "record-store"
    result = subprocess.run([str(compiler), "--store", str(record_store), "--identity",
                            "ku:agent/record", "1.0.0", str(record_source)],
                            cwd=work, env=environment, capture_output=True, timeout=30)
    assert result.returncode != 0 and not record_store.exists()
    for options in [[], ["--recovery"]]:
        run([compiler, "--enable-records", *options, "--store", record_store,
             "--identity", "ku:agent/record", "1.0.0", record_source])
    run([module_cli, "--enable-records", "verify", record_store, "ku:agent/record", "1.0.0"])
    assert run([module_cli, "--enable-records", "run", record_store,
                "ku:agent/record", "1.0.0", "main"]) == "42"
    result = subprocess.run([str(module_cli), "verify", str(record_store), "ku:agent/record", "1.0.0"],
                            cwd=work, env=environment, capture_output=True, timeout=30)
    assert result.returncode != 0
    run([compiler, "--enable-records", "--store", record_store, "--identity", "ku:agent/record",
         "2.0.0", "--parent", "ku:agent/record", "1.0.0", record_source])
    run([module_cli, "--enable-records", "verify", record_store, "ku:agent/record", "2.0.0"])

    tests = work / "native_tests.ku"
    tests.write_text('思 keysx(值) { 返 值 + 1 }\n'
                     '思 测试_安装包() { 名称 = keys({"z": 1, "a": 2}); '
                     '返 41 + 1 == 42 and keysx(41) == 42 and 名称[0] == "a" and len(keys({})) == 0 }\n',
                     encoding="utf-8")
    for options in [[], ["--recovery"]]:
        output = run([compiler, "--test", *options, tests])
        assert "1 passed, 0 failed, 1 total" in output

    broken = work / "broken.ku"
    broken.write_bytes("思 main() {\n  返 缺失\n}\n".encode("utf-8"))
    for options in [[], ["--recovery"]]:
        result = subprocess.run(
            [str(compiler), "--check", *options, str(broken)], cwd=work, env=environment,
            capture_output=True, text=True, encoding="utf-8", timeout=30,
        )
        assert result.returncode == 1 and not result.stdout, result.stderr
        assert ":2:5: error:" in result.stderr, result.stderr
        assert "2 |   返 缺失" in result.stderr and "^" in result.stderr, result.stderr

    validator = prefix / "bin" / ("ku-config" + suffix)
    examples = prefix / "share" / "dao" / "examples"
    record_outputs = []
    for options in [[], ["--recovery"]]:
        record_example = examples / "record_tests.ku"
        run([compiler, "--check", *options, record_example])
        output = run([compiler, "--test", *options, record_example])
        assert "2 passed, 0 failed, 2 total" in output
        target = work / f"record-{len(record_outputs)}.dao"
        run([compiler, *options, record_example, target])
        record_outputs.append(target.read_bytes())
    assert record_outputs[0] == record_outputs[1]
    assert record_outputs[0][4:8] == b"\x03\x00\x0b\x00"
    (work / "record_dep.ku").write_text(
        '型 Point { Point: 整数, read: 整数 }\n思 read() { 返 2 }\n'
        '思 make() { 返 Point { Point: 40, read: read() } }', encoding="utf-8")
    alias_source = work / "alias_records.ku"
    alias_source.write_text('引 "record_dep" 别 a\n引 "record_dep" 别 b\n'
                            '思 测试_别名() { 返 a_make().Point + b_make().read == 42 }', encoding="utf-8")
    for options in [[], ["--recovery"]]:
        assert "1 passed, 0 failed, 1 total" in run([compiler, "--test", *options, alias_source])
    aot = prefix / "bin" / ("dao-aot" + suffix)
    for source_text in ['型 标记 {}\n思 main() { 返 42 }', record_example.read_text(encoding="utf-8")]:
        record_source = work / "record_aot.ku"
        record_source.write_text(source_text, encoding="utf-8")
        record_module = work / "record_aot.dao"
        run([compiler, record_source, record_module])
        protected_output = work / "record_aot.c"
        protected_output.write_bytes(b"preserve existing output")
        result = subprocess.run([str(aot), str(record_module), str(protected_output)],
                                cwd=work, env=environment, capture_output=True,
                                text=True, encoding="utf-8", timeout=30)
        assert result.returncode != 0 and "ABI11" in result.stderr, result.stderr
        assert protected_output.read_bytes() == b"preserve existing output"
    assert run([validator, examples / "service.conf"]).startswith("valid ")
    for options in [[], ["--recovery"]]:
        output = run([compiler, "--test", *options, examples / "config_validator_tests.ku"])
        assert "12 passed, 0 failed, 12 total" in output
    invalid_config = work / "invalid.conf"
    invalid_config.write_bytes(b"name=service\nport=0\n")
    result = subprocess.run([str(validator), str(invalid_config)], cwd=work, env=environment,
                            capture_output=True, text=True, encoding="utf-8", timeout=30)
    assert result.returncode == 1 and not result.stdout, result.stderr
    assert ":2: error:" in result.stderr and "[port]" in result.stderr, result.stderr
    validator_module = examples / "config_validator.dao"
    original_module = validator_module.read_bytes()
    assert original_module[4:8] == b"\x03\x00\x0b\x00"
    try:
        validator_module.write_bytes(b"invalid installed configuration module")
        result = subprocess.run([str(validator), str(examples / "service.conf")],
                                cwd=work, env=environment, capture_output=True, timeout=30)
        assert result.returncode == 2, "installed configuration module was bypassed"
    finally:
        validator_module.write_bytes(original_module)

    # A corrupt installed compiler must be used, exposing any source-tree fallback.
    compiler_source = prefix / "share" / "dao" / "selfhost" / "compiler.ku"
    original = compiler_source.read_bytes()
    try:
        compiler_source.write_text("invalid installed compiler", encoding="utf-8")
        result = subprocess.run(
            [str(compiler), "--check", str(source)], cwd=work, env=environment,
            capture_output=True, timeout=30,
        )
        assert result.returncode != 0, "installed compiler was bypassed"
    finally:
        compiler_source.write_bytes(original)

    child = """
from pathlib import Path
import sys
from dao_kernel import CodeMemory, Runtime, Thought, DaoKernelError, DAO_FEATURE_RECORDS
from dao_kernel.thought import _find_kernel_library, _find_dao_ku
prefix = Path(sys.argv[1]).resolve()
assert _find_kernel_library().is_relative_to(prefix)
assert _find_dao_ku().is_relative_to(prefix)
thought = Thought.from_source('release-check', '思 main(x) { 返 x + 1 }')
with Runtime(_find_kernel_library()) as runtime:
    assert thought.call_i64(runtime, [41]) == 42
memory = CodeMemory(Path.cwd() / 'python-store')
memory.save_source('思 main() { 返 41 }', 'ku:agent/parent')
memory.save_derived_source('思 main() { 返 42 }', 'ku:agent/child', (1, 0, 0),
                           'ku:agent/parent', (1, 0, 0))
assert memory.run('ku:agent/child') == 42
record = Thought.from_source('installed-record', '型 点 { 横: 整数 } 思 main() { 返 点 { 横: 42 } }')
with Runtime(_find_kernel_library()) as runtime:
    try:
        record.call(runtime)
    except DaoKernelError:
        pass
    else:
        raise AssertionError('default Python runtime accepted Record')
with Runtime(_find_kernel_library(), features=DAO_FEATURE_RECORDS) as runtime:
    result = record.call(runtime)
    owner, type_index = runtime.record_type(result)
    with owner:
        assert owner.required_features == DAO_FEATURE_RECORDS
        assert owner.record_name(type_index) == '点'
        assert owner.record_field_info(type_index, 0) == ('横', 1)
        assert runtime.record_get(result, 0).payload == 42
record_memory = CodeMemory(Path.cwd() / 'python-record-store', features=DAO_FEATURE_RECORDS)
record_memory.save_source('型 点 { 横: 整数 } 思 main() { 值 = 点 { 横: 42 }; 返 值.横 }', 'ku:agent/record')
assert record_memory.run('ku:agent/record') == 42
print('installed Python binding and code memory passed')
print('installed Python Record queries, policy and code memory passed')
"""
    print(run([sys.executable, "-c", child, prefix]))
    rustc = shutil.which("rustc")
    dlltool = shutil.which("llvm-dlltool")
    if rustc and (os.name != "nt" or dlltool):
        rust_library_name = "libdao_kernel.dll" if os.name == "nt" else (
            "libdao_kernel.dylib" if sys.platform == "darwin" else "libdao_kernel.so")
        rust_library = next(prefix / directory / rust_library_name for directory in ["bin", "lib", "lib64"]
                            if (prefix / directory / rust_library_name).exists())
        print(run([sys.executable, prefix / "bindings/rust/test_records.py", rustc,
                   rust_library, compiler, dlltool or ""]))
    print("installed native diagnostics/test/check/fmt/compile/store/verify/run passed")
    print("installed Ku configuration validator and native tests passed")
    print("installed Record frontends, byte identity and native tests passed")
    print("installed numeric AOT rejects ABI11 and preserves existing output")
    print("installed Record store opt-in, default rejection and derived provenance passed")


if __name__ == "__main__":
    artifact = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ku-installed-sdk-") as temporary:
        work = Path(temporary)
        prefix = artifact
        if artifact.suffix == ".zip":
            with zipfile.ZipFile(artifact) as archive:
                archive.extractall(work / "sdk")
            roots = list((work / "sdk").iterdir())
            assert len(roots) == 1 and roots[0].is_dir(), "expected one SDK root"
            prefix = roots[0]
            # ZIP extraction may omit executable mode bits on Unix.
            if os.name != "nt":
                for tool in (prefix / "bin").iterdir():
                    tool.chmod(tool.stat().st_mode | 0o111)
        verify(prefix, work)
    if artifact.suffix == ".zip":
        with artifact.open("rb") as source:
            checksum = hashlib.file_digest(source, "sha256").hexdigest()
        artifact.with_suffix(".zip.sha256").write_text(
            f"{checksum}  {artifact.name}\n", encoding="utf-8"
        )
