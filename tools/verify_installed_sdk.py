"""Validate an installed SDK or release ZIP from an unrelated working directory."""
import hashlib
import os
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
from dao_kernel import CodeMemory, Runtime, Thought
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
print('installed Python binding and code memory passed')
"""
    print(run([sys.executable, "-c", child, prefix]))
    print("installed native check/fmt/compile/store/verify/run passed")


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
