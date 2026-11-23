"""Numeric AOT must reject even unused Record declarations before touching output."""
from pathlib import Path
import subprocess
import sys
import tempfile

compiler, aot = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="ku-record-aot-") as temporary:
    directory = Path(temporary)
    for body in ['返 42', '值 = 标记 {}; 返 42']:
        source = directory / "main.ku"
        source.write_text('型 标记 {}\n思 main() { ' + body + ' }', encoding="utf-8")
        module = directory / "main.dao"
        result = subprocess.run([compiler, str(source), str(module)], capture_output=True,
                                text=True, encoding="utf-8", timeout=30)
        assert result.returncode == 0, result.stderr
        assert module.read_bytes()[4:8] == b"\x03\x00\x0b\x00"
        for exists in [False, True]:
            output = directory / ("existing.c" if exists else "absent.c")
            if exists:
                output.write_bytes(b"keep this output")
            result = subprocess.run([aot, str(module), str(output)], capture_output=True,
                                    text=True, encoding="utf-8", timeout=30)
            assert result.returncode != 0 and "ABI11" in result.stderr, result.stderr
            assert output.read_bytes() == b"keep this output" if exists else not output.exists()
print("Record AOT rejection and output preservation passed")
