"""Generate portable C VM bytecode snapshots for runtime profile modules."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from dao.c_vm_runtime import (  # noqa: E402
    DEFAULT_BOOTSTRAP,
    PROFILES,
    SNAPSHOT_DIR,
    SNAPSHOT_FORMAT_VERSION,
    VM_ABI,
)
from dao.compiler import DaoCompiler  # noqa: E402
from dao.dao_lexer import lex  # noqa: E402
from dao.dao_parser import parse_tokens_as_nodes  # noqa: E402
from dao.runtime import Node  # noqa: E402


def parse_source(raw: bytes) -> Node:
    tokens = lex(raw.decode("utf-8"))
    tokens.append({"type": "eof", "value": "", "line": 0, "col": 0, "pos": 0})
    return parse_tokens_as_nodes(tokens)


def imported_module(source_spec: str) -> Path:
    relative = source_spec[4:] if source_spec.startswith("std/") else source_spec
    path = ROOT / "dao" / "std" / relative
    return path if path.suffix == ".ku" else path.with_suffix(".ku")


def alias_wrappers(import_node: Node) -> list[Node]:
    alias = str(import_node.value)
    source_spec = str(import_node.children[0].value)
    imported_ast = parse_source(imported_module(source_spec).read_bytes())
    wrappers = []
    for thought in imported_ast.children:
        name = str(thought.value)
        if thought.type != "thought" or name.startswith("_") or name.startswith(f"{alias}_"):
            continue
        params = [child for child in thought.children if child.type != "block"]
        args = [Node("ref", str(param.value), []) for param in params]
        call = Node("call", "", [Node("ref", name, []), *args])
        wrappers.append(Node(
            "thought",
            f"{alias}_{name}",
            [*params, Node("block", "", [call])],
        ))
    return wrappers


def compile_module(source: Path, bootstrap_sha256: str) -> dict:
    raw = source.read_bytes()
    ast = parse_source(raw)
    children = []
    for child in ast.children:
        if child.type == "import":
            children.extend(alias_wrappers(child))
        else:
            children.append(child)
    exports = [str(child.value) for child in children if child.type == "thought"]
    bytecode = DaoCompiler().compile_ast(Node("block", "", children))
    bytecode["format_version"] = SNAPSHOT_FORMAT_VERSION
    bytecode["vm_abi"] = VM_ABI
    bytecode["bootstrap_sha256"] = bootstrap_sha256
    bytecode["module_id"] = f"std/{source.stem}"
    bytecode["source_sha256"] = hashlib.sha256(raw).hexdigest()
    bytecode["exports"] = exports
    return bytecode


def atomic_write(path: Path, content: str) -> None:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile("wb", dir=path.parent, delete=False) as handle:
            handle.write(content.encode("utf-8"))
            temporary = Path(handle.name)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main() -> None:
    sources = sorted({path for modules in PROFILES.values() for path in modules})
    SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
    bootstrap_sha256 = hashlib.sha256(DEFAULT_BOOTSTRAP.read_bytes()).hexdigest()
    manifest = {
        "format": "dao-cvm-module-snapshots",
        "format_version": SNAPSHOT_FORMAT_VERSION,
        "vm_abi": VM_ABI,
        "bootstrap_sha256": bootstrap_sha256,
        "modules": {},
    }
    for source in sources:
        bytecode = compile_module(source, bootstrap_sha256)
        snapshot_name = f"{source.stem}.kub.json"
        snapshot_path = SNAPSHOT_DIR / snapshot_name
        snapshot_content = (
            json.dumps(bytecode, ensure_ascii=False, separators=(",", ":")) + "\n"
        )
        atomic_write(
            snapshot_path,
            snapshot_content,
        )
        manifest["modules"][source.name] = {
            "snapshot": snapshot_name,
            "snapshot_sha256": hashlib.sha256(snapshot_content.encode("utf-8")).hexdigest(),
            "source_sha256": bytecode["source_sha256"],
            "module_id": bytecode["module_id"],
            "exports": bytecode["exports"],
        }
        print(f"wrote {snapshot_path.relative_to(ROOT)}")
    atomic_write(
        SNAPSHOT_DIR / "manifest.json",
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
    )


if __name__ == "__main__":
    main()
