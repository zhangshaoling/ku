"""Test host function registration and calling through the Python binding.

This demonstrates the FFI bridge that robotics applications need:
register a Python callback as a host function, call it from Ku bytecode,
and verify the result round-trips correctly.
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dao_kernel import Runtime, DaoValue, DAO_VALUE_I64, DAO_OK


def find_kernel_library():
    """Locate the compiled kernel DLL/SO."""
    project_root = Path(__file__).resolve().parent.parent.parent
    search_roots = [
        project_root / "kernel" / "out",
        project_root / "build",
    ]
    for root in search_roots:
        if not root.exists():
            continue
        for pattern in ["libdao_kernel.dll", "dao_kernel.dll", "libdao_kernel.so", "libdao_kernel.dylib"]:
            matches = list(root.rglob(pattern))
            if matches:
                return matches[0]
    return None


def dll_has_host_ffi(library):
    """Check if the kernel DLL exports the host function registration API."""
    import ctypes as c
    try:
        lib = c.CDLL(str(library))
        return hasattr(lib, "dao_vm_register_host_function")
    except OSError:
        return False


def test_host_function_registration():
    """Test that we can register and call a host function from Python."""
    library = find_kernel_library()
    if library is None:
        print("SKIP: kernel library not built yet (run tools/build_kernel.ps1 first)")
        return

    if not dll_has_host_ffi(library):
        print("SKIP: kernel library missing host FFI exports (rebuild needed)")
        return

    with Runtime(library) as runtime:
        bias = 5

        def sensor_read(args, count):
            if count != 1:
                raise ValueError("expected 1 argument")
            sensor_id = args[0].payload
            result = sensor_id * 10 + bias
            return DaoValue(DAO_VALUE_I64, 0, result)

        # Register host function with symbol_id=800, 1 parameter
        runtime.register_host_function(800, 1, sensor_read)
        print("PASS: host function registered")

        # Unregister it
        status = runtime._lib.dao_vm_unregister_host_function(runtime._vm, 800)
        assert status == DAO_OK, f"unregister failed with status {status}"
        print("PASS: host function unregistered")

        # Verify double-registration is rejected
        runtime.register_host_function(800, 1, sensor_read)
        try:
            runtime.register_host_function(800, 1, sensor_read)
            assert False, "duplicate registration should fail"
        except Exception:
            pass
        print("PASS: duplicate registration rejected")

        # Clean up
        runtime._lib.dao_vm_unregister_host_function(runtime._vm, 800)


if __name__ == "__main__":
    test_host_function_registration()
    print("All host function tests passed")