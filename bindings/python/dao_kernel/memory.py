"""Executable memory on the new kernel: memories are Thoughts (compiled, persistent)."""
from __future__ import annotations

import json
import time
from enum import Enum
from pathlib import Path
from typing import Any, Optional

from .thought import Thought, fnv1a


class MemoryType(Enum):
    SESSION = "session"
    LONG_TERM = "long_term"
    ENTITY = "entity"
    FACT = "fact"


class MemoryEntry:
    """A single memory entry: metadata + executable Thought."""

    def __init__(self, key: str, memory_type: MemoryType, thought: Thought,
                 meta: Optional[dict] = None):
        self.key = key
        self.type = memory_type
        self.thought = thought
        self.meta = meta or {}
        self.created_at = time.time()
        self.accessed_at = time.time()
        self.access_count = 0
        self.strength = 1.0

    def access(self) -> Thought:
        self.accessed_at = time.time()
        self.access_count += 1
        self.strength = min(1.0, self.strength + 0.01)
        return self.thought

    def weaken(self, amount: float = 0.1) -> None:
        self.strength = max(0.0, self.strength - amount)

    def to_dict(self) -> dict:
        return {
            "key": self.key,
            "type": self.type.value,
            "name": self.thought.name,
            "params": self.thought.params,
            "doc": self.thought.doc,
            "strength": self.strength,
            "access_count": self.access_count,
            "created_at": self.created_at,
            "meta": self.meta,
        }


class MemorySystem:
    """File-backed memory system where each entry is an executable Thought.

    Directory layout:
        <directory>/
            <key>.dao      — compiled module bytes
            <key>.json     — metadata (type, strength, timestamps, tags)
    """

    def __init__(self, directory: str | Path):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self._entries: dict[str, MemoryEntry] = {}
        self._load_all()

    def _load_all(self) -> None:
        """Load all existing memories from disk."""
        for json_file in sorted(self.directory.glob("*.json")):
            key = json_file.stem
            dao_file = json_file.with_suffix(".dao")
            if not dao_file.exists():
                continue
            try:
                meta = json.loads(json_file.read_text(encoding="utf-8"))
                memory_type = MemoryType(meta.get("type", "session"))
                thought = Thought.load(dao_file, name=meta.get("name", key),
                                       doc=meta.get("doc", ""),
                                       params=meta.get("params", []))
                entry = MemoryEntry(key, memory_type, thought, meta.get("meta", {}))
                entry.strength = meta.get("strength", 1.0)
                entry.access_count = meta.get("access_count", 0)
                entry.created_at = meta.get("created_at", time.time())
                entry.accessed_at = meta.get("accessed_at", time.time())
                self._entries[key] = entry
            except Exception:
                continue

    def _save_entry(self, entry: MemoryEntry) -> None:
        """Persist a single entry to disk."""
        dao_path = self.directory / f"{entry.key}.dao"
        json_path = self.directory / f"{entry.key}.json"
        entry.thought.save(dao_path)
        meta = entry.to_dict()
        del meta["key"]
        json_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2),
                            encoding="utf-8")

    def store(self, key: str, thought: Thought,
              memory_type: MemoryType = MemoryType.SESSION,
              meta: Optional[dict] = None) -> MemoryEntry:
        """Store a memory (Thought)."""
        entry = MemoryEntry(key, memory_type, thought, meta)
        self._entries[key] = entry
        self._save_entry(entry)
        return entry

    def recall(self, key: str) -> Optional[Thought]:
        """Recall a memory by key. Returns the Thought or None."""
        entry = self._entries.get(key)
        if entry is None:
            return None
        return entry.access()

    def forget(self, key: str) -> bool:
        """Delete a memory. Returns True if found and deleted."""
        if key not in self._entries:
            return False
        dao_path = self.directory / f"{key}.dao"
        json_path = self.directory / f"{key}.json"
        dao_path.unlink(missing_ok=True)
        json_path.unlink(missing_ok=True)
        del self._entries[key]
        return True

    def search(self, query: str, limit: int = 10) -> list[MemoryEntry]:
        """Search memories by key/name/doc/content match."""
        results = []
        query_lower = str(query).lower()
        for key, entry in self._entries.items():
            score = 0
            if query_lower in key.lower():
                score += 3
            if query_lower in entry.thought.name.lower():
                score += 2
            if query_lower in entry.thought.doc.lower():
                score += 1
            if query_lower in str(entry.meta).lower():
                score += 1
            score *= entry.strength
            if score > 0:
                results.append((entry, score))
        results.sort(key=lambda x: x[1], reverse=True)
        return [e for e, _ in results[:limit]]

    def list_all(self) -> list[str]:
        """List all memory keys."""
        return sorted(self._entries.keys())

    def stats(self) -> dict:
        """Return memory system statistics."""
        total = len(self._entries)
        by_type = {}
        for e in self._entries.values():
            by_type[e.type.value] = by_type.get(e.type.value, 0) + 1
        return {
            "total": total,
            "by_type": by_type,
            "avg_strength": (
                sum(e.strength for e in self._entries.values()) / max(1, total)
            ),
        }

    def __len__(self) -> int:
        return len(self._entries)

    def __contains__(self, key: str) -> bool:
        return key in self._entries
