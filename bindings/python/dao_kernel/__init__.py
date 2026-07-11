"""Minimal Python binding for the stable Dao Kernel C ABI."""
from __future__ import annotations

import ctypes as c
import os
from pathlib import Path

DAO_OK = 0
DAO_VALUE_I64 = 1


class DaoBytes(c.Structure):
    _fields_ = [("data", c.POINTER(c.c_uint8)), ("size", c.c_size_t)]


class DaoValue(c.Structure):
    _fields_ = [("type", c.c_uint32), ("reserved", c.c_uint32), ("payload", c.c_int64)]


class DaoError(c.Structure):
    _fields_ = [("code", c.c_int), ("function_index", c.c_uint32),
                ("instruction_index", c.c_uint32), ("message", c.c_char * 192)]


class DaoConfig(c.Structure):
    _fields_ = [("struct_size", c.c_uint32), ("max_registers", c.c_uint32),
                ("max_call_depth", c.c_uint32), ("reserved", c.c_uint32),
                ("max_module_bytes", c.c_uint64), ("max_instructions_per_call", c.c_uint64)]


class DaoKernelError(RuntimeError):
    pass


class Runtime:
    def __init__(self, library: str | Path):
        library = Path(library).resolve()
        self._dll_dirs = []
        if os.name == "nt":
            for directory in [library.parent, *(Path(p) for p in os.environ.get("DAO_KERNEL_DLL_DIRS", "").split(os.pathsep) if p)]:
                self._dll_dirs.append(os.add_dll_directory(str(directory)))
        self._lib = c.CDLL(str(library))
        self._bind()
        config = self._lib.dao_vm_config_default()
        self._vm = self._lib.dao_vm_create(c.byref(config))
        if not self._vm:
            raise DaoKernelError("dao_vm_create failed")

    def _bind(self) -> None:
        lib = self._lib
        lib.dao_vm_config_default.restype = DaoConfig
        lib.dao_vm_create.argtypes = [c.POINTER(DaoConfig)]; lib.dao_vm_create.restype = c.c_void_p
        lib.dao_vm_destroy.argtypes = [c.c_void_p]
        lib.dao_vm_load_module.argtypes = [c.c_void_p, DaoBytes, c.POINTER(c.c_void_p), c.POINTER(DaoError)]
        lib.dao_module_release.argtypes = [c.c_void_p]
        lib.dao_module_find_export.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32)]
        lib.dao_vm_call.argtypes = [c.c_void_p, c.c_void_p, c.c_uint32, c.POINTER(DaoValue), c.c_size_t, c.POINTER(DaoValue), c.POINTER(DaoError)]

    def load(self, payload: bytes) -> "Module":
        storage = (c.c_uint8 * len(payload)).from_buffer_copy(payload)
        handle = c.c_void_p(); error = DaoError()
        status = self._lib.dao_vm_load_module(self._vm, DaoBytes(storage, len(payload)), c.byref(handle), c.byref(error))
        if status != DAO_OK:
            raise DaoKernelError(error.message.decode("utf-8", "replace"))
        return Module(self, handle)

    def close(self) -> None:
        if self._vm:
            self._lib.dao_vm_destroy(self._vm); self._vm = None

    def __enter__(self) -> "Runtime": return self
    def __exit__(self, *_: object) -> None: self.close()


class Module:
    def __init__(self, runtime: Runtime, handle: c.c_void_p): self._runtime, self._handle = runtime, handle
    def call_i64(self, symbol: int, *values: int) -> int:
        function = c.c_uint32(); lib = self._runtime._lib
        if lib.dao_module_find_export(self._handle, symbol, c.byref(function)) != DAO_OK:
            raise DaoKernelError(f"export {symbol} not found")
        args = (DaoValue * len(values))(*(DaoValue(DAO_VALUE_I64, 0, value) for value in values))
        result, error = DaoValue(), DaoError()
        status = lib.dao_vm_call(self._runtime._vm, self._handle, function, args, len(values), c.byref(result), c.byref(error))
        if status != DAO_OK: raise DaoKernelError(error.message.decode("utf-8", "replace"))
        if result.type != DAO_VALUE_I64: raise DaoKernelError("result is not i64")
        return result.payload
    def close(self) -> None:
        if self._handle: self._runtime._lib.dao_module_release(self._handle); self._handle = None
    def __enter__(self) -> "Module": return self
    def __exit__(self, *_: object) -> None: self.close()
