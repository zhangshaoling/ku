"""Compile Rust sys bindings and call their freestanding native Record probe."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import sys
import tempfile

rustc, library, compiler = (str(Path(arg).resolve()) for arg in sys.argv[1:4])
crate = Path(__file__).parent / "dao-kernel-sys"


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
                            encoding="utf-8", timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout.strip()


with tempfile.TemporaryDirectory(prefix="ku-rust-record-") as temporary:
    work = Path(temporary)
    source = work / "record.ku"
    source.write_text('型 Point { x: 整数, ok: 三值, name: 文本, data: 字节 }\n'
                      '思 main(blob) { 返 Point { x: 42, ok: 真, name: "中文", data: blob } }\n'
                      '思 number() { 返 42 }', encoding="utf-8")
    payload = work / "record.dao"
    run([compiler, source, payload])
    rlib = work / "libdao_kernel_sys.rlib"
    run([rustc, "--edition=2021", "--crate-name=dao_kernel_sys", "--crate-type=rlib",
         crate / "src/lib.rs", "-o", rlib])
    options = []
    if os.name == "nt":
        # MSVC Rust can test the LLVM-MinGW DLL without an installed Visual C++ CRT.
        # Export names come from the public kernel header, not a separate ABI copy.
        import re
        header = Path(library).parents[1] / "include/dao/dao.h"
        if not header.exists():
            header = crate.parents[2] / "kernel/include/dao/dao.h"
        names = re.findall(r"DAO_API\s+\w+[\s*]+(dao_\w+)\s*\(", header.read_text(encoding="utf-8"))
        definition = work / "dao_kernel.def"
        definition.write_text("LIBRARY " + Path(library).name + "\nEXPORTS\n" + "\n".join(names), encoding="utf-8")
        dlltool = sys.argv[4]
        run([dlltool, "-m", "i386:x86-64", "-d", definition, "-l", work / "dao_kernel.lib"])
        sysroot = Path(run([rustc, "--print", "sysroot"]))
        host = next(line.split(": ")[1] for line in run([rustc, "-vV"]).splitlines() if line.startswith("host: "))
        linker = sysroot / "lib/rustlib" / host / "bin/rust-lld.exe"
        options = ["-C", f"linker={linker}", "-C", "linker-flavor=lld-link",
                   "-C", "link-arg=/NOENTRY", "-C", "default-linker-libraries=no"]
        suffix = ".dll"
    else:
        suffix = ".dylib" if sys.platform == "darwin" else ".so"
        options = ["-L", f"native={Path(library).parent}"]
    probe = work / ("record_probe" + suffix)
    run([rustc, "--edition=2021", "--crate-type=cdylib", "-C", "panic=abort", "-C", "opt-level=2",
         crate / "record_probe.rs", "--extern", f"dao_kernel_sys={rlib}",
         "-L", f"native={work}", "-l", "dylib=dao_kernel", *options, "-o", probe])
    dll_directories = []
    if os.name == "nt":
        dll_directories.append(os.add_dll_directory(str(Path(library).parent)))
        for path in os.environ.get("DAO_KERNEL_DLL_DIRS", "").split(os.pathsep):
            if path:
                dll_directories.append(os.add_dll_directory(path))
    kernel = c.CDLL(library, mode=c.RTLD_GLOBAL)
    binding = c.CDLL(str(probe))
    binding.dao_record_binding_probe.argtypes = [c.c_void_p, c.c_size_t]
    data = payload.read_bytes()
    storage = c.create_string_buffer(data)
    result = binding.dao_record_binding_probe(storage, len(data))
    assert result == 0, f"Rust Record probe failed at line {result}"
    # Windows keeps loaded DLLs locked; release before TemporaryDirectory cleanup.
    if os.name == "nt":
        import _ctypes
        _ctypes.FreeLibrary(binding._handle)
    print("Rust Record binding execution, policy, descriptors and lifetime passed")
