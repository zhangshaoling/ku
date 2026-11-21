"""Persistent scalar memory Host capabilities for the Ku runtime."""
from __future__ import annotations

import json
from pathlib import Path

from . import DAO_VALUE_I64, DaoValue, Runtime, fnv1a


class MemoryHost:
    """Expose ``memory_store/recall/forget`` as i64 Host imports.

    The storage contract for this first language-native slice is UTF-8 string keys
    and signed 64-bit values. The JSON file is host-owned persistence; the VM only
    sees scalar values and never receives a borrowed view into the file buffer.
    """

    def __init__(self, runtime: Runtime, directory: str | Path):
        self.runtime = runtime
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.path = self.directory / "host_values.json"
        self.values: dict[str, int] = self._load()
        self._callbacks: dict[str, object] = {}
        self.attach()

    def _load(self) -> dict[str, int]:
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except (FileNotFoundError, ValueError, OSError):
            return {}
        if not isinstance(raw, dict):
            return {}
        result: dict[str, int] = {}
        for key, value in raw.items():
            if isinstance(key, str) and isinstance(value, int) and not isinstance(value, bool):
                result[key] = value
        return result

    def _save(self) -> None:
        temporary = self.path.with_suffix(".tmp")
        temporary.write_text(
            json.dumps(self.values, ensure_ascii=False, sort_keys=True), encoding="utf-8"
        )
        temporary.replace(self.path)

    def _key(self, value: DaoValue) -> str:
        if value.type != 4:
            raise TypeError("memory key must be a string")
        return self.runtime.value_text(value)

    @staticmethod
    def _i64(value: DaoValue) -> int:
        if value.type != DAO_VALUE_I64:
            raise TypeError("memory value must be i64")
        return int(value.payload)

    def _store(self, args, count, _user_data=None):
        if count != 2:
            raise ValueError("memory_store expects key and value")
        key = self._key(args[0])
        value = self._i64(args[1])
        self.values[key] = value
        self._save()
        return DaoValue(DAO_VALUE_I64, 0, value)

    def _recall(self, args, count, _user_data=None):
        if count != 1:
            raise ValueError("memory_recall expects key")
        value = self.values.get(self._key(args[0]), 0)
        return DaoValue(DAO_VALUE_I64, 0, value)

    def _forget(self, args, count, _user_data=None):
        if count != 1:
            raise ValueError("memory_forget expects key")
        removed = self.values.pop(self._key(args[0]), None) is not None
        if removed:
            self._save()
        return DaoValue(DAO_VALUE_I64, 0, 1 if removed else 0)

    def attach(self) -> None:
        self._callbacks = {
            "memory_store": self.runtime.register_host_import("memory_store", 2, self._store),
            "memory_recall": self.runtime.register_host_import("memory_recall", 1, self._recall),
            "memory_forget": self.runtime.register_host_import("memory_forget", 1, self._forget),
        }

    def close(self) -> None:
        for name in tuple(self._callbacks):
            self.runtime.unregister_host_function(fnv1a(name))
        self._callbacks.clear()
