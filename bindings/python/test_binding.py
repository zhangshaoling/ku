import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dao_kernel import Runtime

library, module_path = sys.argv[1:]
symbol = 2166136261
for byte in b"add":
    symbol = ((symbol ^ byte) * 16777619) & 0xFFFFFFFF
with Runtime(library) as runtime:
    with runtime.load(Path(module_path).read_bytes()) as module:
        assert module.call_i64(symbol, 40, 2) == 42
