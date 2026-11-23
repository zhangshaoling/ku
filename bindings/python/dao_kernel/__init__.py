"""Minimal Python binding for the stable Ku Kernel C ABI."""
from __future__ import annotations

import ctypes as c
import inspect
import os
from pathlib import Path

DAO_OK = 0
DAO_UNSUPPORTED_FEATURE = 17
DAO_FEATURE_RECORDS = 1
DAO_VALUE_NULL = 0
DAO_VALUE_I64 = 1
DAO_VALUE_TRIT = 2
DAO_VALUE_BYTES = 3
DAO_VALUE_STRING = 4
DAO_VALUE_LIST = 5
DAO_VALUE_MAP = 6
DAO_VALUE_RECORD = 9


def _feature_mask(features: int) -> int:
    if not isinstance(features, int) or features < 0 or features & ~DAO_FEATURE_RECORDS:
        raise ValueError("unknown kernel feature mask")
    return features


def _record_index(index: int) -> int:
    if not isinstance(index, int) or not 0 <= index <= 0xFFFFFFFF:
        raise ValueError("Record index must fit uint32")
    return index


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
    def __init__(self, library: str | Path, *, features: int = 0):
        features = _feature_mask(features)
        library = Path(library).resolve()
        self._dll_dirs = []
        self._host_callbacks = {}
        if os.name == "nt":
            for directory in [library.parent, *(Path(p) for p in os.environ.get("DAO_KERNEL_DLL_DIRS", "").split(os.pathsep) if p)]:
                self._dll_dirs.append(os.add_dll_directory(str(directory)))
        self._lib = c.CDLL(str(library))
        self._bind()
        config = self._lib.dao_vm_config_default()
        self._vm = self._lib.dao_vm_create(c.byref(config))
        if not self._vm:
            raise DaoKernelError("dao_vm_create failed")
        try:
            self.set_features(features)
        except Exception:
            self.close()
            raise

    def _bind(self) -> None:
        lib = self._lib
        lib.dao_vm_config_default.restype = DaoConfig
        lib.dao_vm_create.argtypes = [c.POINTER(DaoConfig)]; lib.dao_vm_create.restype = c.c_void_p
        lib.dao_vm_destroy.argtypes = [c.c_void_p]
        lib.dao_vm_destroy.restype = None
        lib.dao_vm_set_features.argtypes = [c.c_void_p, c.c_uint64]
        lib.dao_vm_get_features.argtypes = [c.c_void_p, c.POINTER(c.c_uint64)]
        lib.dao_module_required_features.argtypes = [c.c_void_p, c.POINTER(c.c_uint64)]
        lib.dao_module_record_count.argtypes = [c.c_void_p, c.POINTER(c.c_uint32)]
        lib.dao_module_record_name.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(DaoBytes)]
        lib.dao_module_record_field_count.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32)]
        lib.dao_module_record_field_info.argtypes = [c.c_void_p, c.c_uint32, c.c_uint32, c.POINTER(DaoBytes), c.POINTER(c.c_uint32)]
        lib.dao_value_record_type.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.POINTER(c.c_void_p), c.POINTER(c.c_uint32)]
        lib.dao_value_record_get.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.c_uint32, c.POINTER(DaoValue)]
        lib.dao_vm_clear_module_cache.argtypes = [c.c_void_p]
        lib.dao_vm_load_module.argtypes = [c.c_void_p, DaoBytes, c.POINTER(c.c_void_p), c.POINTER(DaoError)]
        lib.dao_module_release.argtypes = [c.c_void_p]
        lib.dao_module_release.restype = None
        lib.dao_module_find_export.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32)]
        lib.dao_vm_call.argtypes = [c.c_void_p, c.c_void_p, c.c_uint32, c.POINTER(DaoValue), c.c_size_t, c.POINTER(DaoValue), c.POINTER(DaoError)]
        lib.dao_value_list_size.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.POINTER(c.c_size_t)]
        lib.dao_value_list_get.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.c_size_t, c.POINTER(DaoValue)]
        lib.dao_value_map_get.argtypes = [c.c_void_p, c.POINTER(DaoValue), DaoBytes, c.POINTER(DaoValue)]
        lib.dao_vm_make_list.argtypes = [c.c_void_p, c.POINTER(DaoValue)]
        lib.dao_value_list_append.argtypes = [c.c_void_p, c.POINTER(DaoValue), c.POINTER(DaoValue)]
        lib.dao_vm_make_map.argtypes = [c.c_void_p, c.POINTER(DaoValue)]
        lib.dao_value_map_set.argtypes = [c.c_void_p, c.POINTER(DaoValue), DaoBytes, c.POINTER(DaoValue)]
        lib.dao_value_make_string_view.argtypes = [DaoBytes, c.POINTER(DaoValue)]
        lib.dao_value_make_string_view.restype = c.c_int
        lib.dao_value_get_view.argtypes = [c.POINTER(DaoValue), c.POINTER(DaoBytes)]
        lib.dao_value_get_view.restype = c.c_int
        lib.dao_vm_register_host_function.argtypes = [c.c_void_p, c.POINTER(DaoHostFunction)]
        lib.dao_vm_register_host_function.restype = c.c_int
        lib.dao_vm_unregister_host_function.argtypes = [c.c_void_p, c.c_uint32]
        lib.dao_vm_unregister_host_function.restype = c.c_int

    def set_features(self, features: int) -> None:
        """Set policy before the first accepted load/link/call; the C ABI locks it."""
        status = self._lib.dao_vm_set_features(self._vm, _feature_mask(features))
        if status != DAO_OK:
            raise DaoKernelError(f"set_features failed with status {status}")

    @property
    def features(self) -> int:
        mask = c.c_uint64()
        if self._lib.dao_vm_get_features(self._vm, c.byref(mask)) != DAO_OK:
            raise DaoKernelError("invalid or closed runtime")
        return mask.value

    def record_type(self, value: DaoValue) -> tuple["Module", int]:
        """Return a retained owner and local type index. Close the owner explicitly."""
        owner, index = c.c_void_p(), c.c_uint32()
        status = self._lib.dao_value_record_type(self._vm, c.byref(value), c.byref(owner), c.byref(index))
        if status != DAO_OK:
            raise DaoKernelError(f"invalid, foreign or stale Record type: {status}")
        return Module(self, owner), index.value

    def record_get(self, value: DaoValue, slot: int) -> DaoValue:
        """Read a scalar field. Returned String/Bytes views retain C ABI borrowing rules."""
        result = DaoValue()
        status = self._lib.dao_value_record_get(self._vm, c.byref(value), _record_index(slot), c.byref(result))
        if status != DAO_OK:
            raise DaoKernelError(f"invalid, foreign or stale Record field: {status}")
        return result

    def register_host_function(self, symbol_id, parameter_count, callback, user_data=None):
        """Register a callback receiving ``(args, count, user_data)``.

        The callback may return a DaoValue or raise; exceptions are converted to a
        runtime error by the C ABI bridge. Returned callback references are retained
        until explicit unregistration or Runtime.close().
        """
        @c.CFUNCTYPE(c.c_int, c.c_void_p, c.POINTER(DaoValue), c.c_size_t, c.POINTER(DaoValue))
        def _host_callback(raw_user_data, raw_args, arg_count, out_value):
            try:
                args = [raw_args[i] for i in range(arg_count)] if raw_args else []
                if len(inspect.signature(callback).parameters) >= 3:
                    result = callback(args, arg_count, user_data)
                else:
                    result = callback(args, arg_count)
                out_value[0] = result
                return DAO_OK
            except Exception:
                return 11

        self._host_callbacks = getattr(self, '_host_callbacks', {})
        function = DaoHostFunction(c.sizeof(DaoHostFunction), symbol_id, parameter_count, 0,
                                   c.cast(_host_callback, c.c_void_p).value,
                                   c.c_void_p(id(user_data)) if user_data is not None else None)
        status = self._lib.dao_vm_register_host_function(self._vm, c.byref(function))
        if status != DAO_OK:
            raise DaoKernelError(f"register_host_function failed with status {status}")
        self._host_callbacks[symbol_id] = _host_callback
        return _host_callback

    def unregister_host_function(self, symbol_id):
        status = self._lib.dao_vm_unregister_host_function(self._vm, symbol_id)
        if status == DAO_OK:
            self._host_callbacks.pop(symbol_id, None)
        return status

    def register_host_import(self, name, parameter_count, callback, user_data=None):
        """Register a named Host import using the compiler's FNV-1a symbol ID."""
        return self.register_host_function(fnv1a(name), parameter_count, callback, user_data)

    def make_string(self, value: str) -> DaoValue:
        encoded = value.encode("utf-8")
        storage = (c.c_uint8 * len(encoded)).from_buffer_copy(encoded)
        result = DaoValue()
        status = self._lib.dao_value_make_string_view(DaoBytes(storage, len(encoded)), c.byref(result))
        if status != DAO_OK:
            raise DaoKernelError(f"string value creation failed with status {status}")
        self._view_storage = getattr(self, "_view_storage", [])
        self._view_storage.append(storage)
        return result

    def value_text(self, value: DaoValue) -> str:
        return self.value_bytes(value).decode("utf-8")

    def value_bytes(self, value: DaoValue) -> bytes:
        """Copy a live borrowed String/Bytes view into Python-owned bytes."""
        view = DaoBytes()
        status = self._lib.dao_value_get_view(c.byref(value), c.byref(view))
        if status != DAO_OK:
            raise DaoKernelError(f"value is not a valid bytes/string view: {status}")
        return bytes(view.data[:view.size]) if view.size else b""

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
            memory_host = getattr(self, "_ku_memory_host", None)
            if memory_host is not None:
                memory_host.close()
                self._ku_memory_host = None
            self._host_callbacks.clear()
            self._lib.dao_vm_destroy(self._vm); self._vm = None

    def __enter__(self) -> "Runtime": return self
    def __exit__(self, *_: object) -> None: self.close()


class Module:
    def __init__(self, runtime: Runtime, handle: c.c_void_p): self._runtime, self._handle = runtime, handle

    @property
    def required_features(self) -> int:
        mask = c.c_uint64()
        if self._runtime._lib.dao_module_required_features(self._handle, c.byref(mask)) != DAO_OK:
            raise DaoKernelError("invalid or closed module")
        return mask.value

    @property
    def record_count(self) -> int:
        count = c.c_uint32()
        if self._runtime._lib.dao_module_record_count(self._handle, c.byref(count)) != DAO_OK:
            raise DaoKernelError("invalid or closed module")
        return count.value

    def record_name(self, type_index: int) -> str:
        name = DaoBytes()
        status = self._runtime._lib.dao_module_record_name(self._handle, _record_index(type_index), c.byref(name))
        if status != DAO_OK:
            raise DaoKernelError(f"invalid Record type: {status}")
        return bytes(name.data[:name.size]).decode("utf-8")

    def record_field_count(self, type_index: int) -> int:
        count = c.c_uint32()
        status = self._runtime._lib.dao_module_record_field_count(self._handle, _record_index(type_index), c.byref(count))
        if status != DAO_OK:
            raise DaoKernelError(f"invalid Record type: {status}")
        return count.value

    def record_field_info(self, type_index: int, slot: int) -> tuple[str, int]:
        name, value_type = DaoBytes(), c.c_uint32()
        status = self._runtime._lib.dao_module_record_field_info(
            self._handle, _record_index(type_index), _record_index(slot), c.byref(name), c.byref(value_type))
        if status != DAO_OK:
            raise DaoKernelError(f"invalid Record field: {status}")
        return bytes(name.data[:name.size]).decode("utf-8"), value_type.value

    def call_i64(self, symbol: int, *values: int) -> int:
        result = self.call(symbol, *values)
        if result.type != DAO_VALUE_I64: raise DaoKernelError("result is not i64")
        return result.payload
    def call(self, symbol: int, *values) -> DaoValue:
        if any(not isinstance(value, (int, str)) for value in values):
            raise DaoKernelError("call accepts only int/str arguments; Record and container handles cannot be reused")
        function = c.c_uint32(); lib = self._runtime._lib
        if lib.dao_module_find_export(self._handle, symbol, c.byref(function)) != DAO_OK:
            raise DaoKernelError(f"export {symbol} not found")
        converted = [self._runtime.make_string(value) if isinstance(value, str)
                     else DaoValue(DAO_VALUE_I64, 0, value) for value in values]
        args = (DaoValue * len(converted))(*converted)
        result, error = DaoValue(), DaoError()
        status = lib.dao_vm_call(self._runtime._vm, self._handle, function, args, len(converted), c.byref(result), c.byref(error))
        if status != DAO_OK: raise DaoKernelError(error.message.decode("utf-8", "replace"))
        return result
    def close(self) -> None:
        if self._handle: self._runtime._lib.dao_module_release(self._handle); self._handle = None
    def __enter__(self) -> "Module": return self
    def __exit__(self, *_: object) -> None: self.close()

from .thought import Thought, fnv1a
from .memory import MemorySystem, MemoryEntry, MemoryType
from .task import Task, TaskPlanner, TaskPriority, TaskStatus
from .agent import Agent, AgentState, AgentPhase, Toolbox
from .code_memory import CodeMemory
