"""Minimal Python binding for the stable Ku Kernel C ABI."""
from __future__ import annotations

import ctypes as c
import os
from pathlib import Path

DAO_OK = 0
DAO_VALUE_I64 = 1
DAO_VALUE_BYTES = 3
DAO_VALUE_STRING = 4
DAO_VALUE_LIST = 5
DAO_VALUE_MAP = 6


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


class DaoHostFunction(c.Structure):
    _fields_ = [("struct_size", c.c_uint32), ("symbol_id", c.c_uint32),
                ("parameter_count", c.c_uint32), ("reserved", c.c_uint32),
                ("callback", c.c_void_p), ("user_data", c.c_void_p)]


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
        lib.dao_value_list_size.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.POINTER(c.c_size_t)]
        lib.dao_value_list_get.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.c_size_t, c.POINTER(DaoValue)]
        lib.dao_value_map_get.argtypes = [c.c_void_p, c.POINTER(DaoValue), DaoBytes, c.POINTER(DaoValue)]
        lib.dao_vm_make_list.argtypes = [c.c_void_p, c.POINTER(DaoValue)]
        lib.dao_value_list_append.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.POINTER(DaoValue)]
        lib.dao_vm_make_map.argtypes = [c.c_void_p, c.POINTER(DaoValue)]
        lib.dao_value_map_set.argtypes = [c.c_void_p, c.POINTER(DaoValue), DaoBytes, c.POINTER(DaoValue)]
        lib.dao_vm_register_host_function.argtypes = [c.c_void_p, c.POINTER(DaoHostFunction)]
        lib.dao_vm_register_host_function.restype = c.c_int
        lib.dao_vm_unregister_host_function.argtypes = [c.c_void_p, c.c_uint32]
        lib.dao_vm_unregister_host_function.restype = c.c_int

    def register_host_function(self, symbol_id, parameter_count, callback, user_data=None):
        """Register a host function callable from Ku bytecode.

        callback signature: (args: list[DaoValue], count: int) -> DaoValue
        The callback receives a list of DaoValue arguments and must return a DaoValue.
        """
        @c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(DaoValue), c.c_size_t, c.POINTER(DaoValue))
        def _host_callback(user_data, raw_args, arg_count, out_value):
            try:
                args = [raw_args[i] for i in range(arg_count)] if raw_args else []
                result = callback(args, arg_count)
                out_value[0] = result
                return 0
            except Exception:
                return 11

        self._host_callbacks = getattr(self, '_host_callbacks', [])
        self._host_callbacks.append(_host_callback)
        function = DaoHostFunction(
            struct_size=c.sizeof(DaoHostFunction),
            symbol_id=symbol_id,
            parameter_count=parameter_count,
            reserved=0,
            callback=c.cast(_host_callback, c.c_void_p).value,
            user_data=None
        )
        status = self._lib.dao_vm_register_host_function(self._vm, c.byref(function))
        if status != DAO_OK:
            raise DaoKernelError(f"register_host_function failed with status {status}")
        return _host_callback

    def load(self, payload: bytes) -> "Module":
        storage = (c.c_uint8 * len(payload)).from_buffer_copy(payload)
        handle = c.c_void_p(); error = DaoError()
        status = self._lib.dao_vm_load_module(self._vm, DaoBytes(storage, len(payload)), c.byref(handle), c.byref(error))
        if status != DAO_OK:
            raise DaoKernelError(error.message.decode("utf-8", "replace"))
        return Module(self, handle)

    def list_values(self, value: DaoValue) -> list[DaoValue]:
        size = c.c_size_t()
        if self._lib.dao_value_list_size(self._vm, c.byref(value), c.byref(size)) != DAO_OK:
            raise DaoKernelError("invalid or stale list handle")
        result = []
        for index in range(size.value):
            item = DaoValue()
            if self._lib.dao_value_list_get(self._vm, c.byref(value), index, c.byref(item)) != DAO_OK:
                raise DaoKernelError("list access failed")
            result.append(item)
        return result

    def map_value(self, value: DaoValue, key: str) -> DaoValue:
        encoded = key.encode("utf-8"); storage = (c.c_uint8 * len(encoded)).from_buffer_copy(encoded)
        result = DaoValue()
        if self._lib.dao_value_map_get(self._vm, c.byref(value), DaoBytes(storage, len(encoded)), c.byref(result)) != DAO_OK:
            raise DaoKernelError(f"map key {key!r} not found or handle is stale")
        return result

    def close(self) -> None:
        if self._vm:
            self._lib.dao_vm_destroy(self._vm); self._vm = None

    def __enter__(self) -> "Runtime": return self
    def __exit__(self, *_: object) -> None: self.close()


class Module:
    def __init__(self, runtime: Runtime, handle: c.c_void_p): self._runtime, self._handle = runtime, handle
    def call_i64(self, symbol: int, *values: int) -> int:
        result = self.call(symbol, *values)
        if result.type != DAO_VALUE_I64: raise DaoKernelError("result is not i64")
        return result.payload
    def call(self, symbol: int, *values: int) -> DaoValue:
        function = c.c_uint32(); lib = self._runtime._lib
        if lib.dao_module_find_export(self._handle, symbol, c.byref(function)) != DAO_OK:
            raise DaoKernelError(f"export {symbol} not found")
        args = (DaoValue * len(values))(*(DaoValue(DAO_VALUE_I64, 0, value) for value in values))
        result, error = DaoValue(), DaoError()
        status = lib.dao_vm_call(self._runtime._vm, self._handle, function, args, len(values), c.byref(result), c.byref(error))
        if status != DAO_OK: raise DaoKernelError(error.message.decode("utf-8", "replace"))
        return result
    def close(self) -> None:
        if self._handle: self._runtime._lib.dao_module_release(self._handle); self._handle = None
    def __enter__(self) -> "Module": return self
    def __exit__(self, *_: object) -> None: self.close()

from .thought import Thought, fnv1a
from .memory import MemorySystem, MemoryEntry, MemoryType
from .task import Task, TaskPlanner, TaskPriority, TaskStatus
