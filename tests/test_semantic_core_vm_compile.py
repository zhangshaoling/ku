from pathlib import Path

import pytest

from dao.compiler import DaoVM
from dao.dao_lexer import lex
from dao.dao_parser import parse_tokens
from dao.runtime import DaoEnv, Thought


ROOT = Path(__file__).resolve().parents[1]

SEMANTIC_STD_FILES = [
    ROOT / "dao" / "std" / "semantic_core.ku",
    ROOT / "dao" / "std" / "trace.ku",
    ROOT / "dao" / "std" / "patch.ku",
    ROOT / "dao" / "std" / "memory_env.ku",
]


def parse_file(path):
    tokens = lex(path.read_text(encoding="utf-8"))
    tokens.append({"type": "eof", "value": "", "line": 0, "col": 0, "pos": 0})
    return parse_tokens(tokens)


@pytest.fixture(autouse=True)
def load_compiler():
    Thought.registry.clear()
    env = DaoEnv()
    env.load(str(ROOT / "dao" / "std" / "compiler.ku"))


@pytest.mark.parametrize("path", SEMANTIC_STD_FILES, ids=lambda p: p.name)
def test_semantic_std_file_compiles_to_bytecode(path):
    ast = parse_file(path)

    bytecode = Thought.registry["compile_ast"].call([ast])

    assert bytecode["version"] == "0.1"
    assert bytecode["instructions"]
    assert isinstance(bytecode["constants"], list)
    assert any(instr[0] == "MAKE_FUNCTION" for instr in bytecode["instructions"])


def test_compiled_semantic_core_function_executes_in_vm():
    ast = parse_file(ROOT / "dao" / "std" / "semantic_core.ku")
    ast["children"].append(
        {
            "type": "call",
            "value": "语义_构造无参思",
            "children": [
                {"type": "literal", "value": "check_state"},
                {
                    "type": "list",
                    "children": [
                        {"type": "literal", "value": "observe system.ready"},
                        {"type": "literal", "value": "verify"},
                    ],
                },
            ],
        }
    )

    bytecode = Thought.registry["compile_ast"].call([ast])
    result = DaoVM().execute(bytecode)

    assert result["type"] == "thought"
    assert result["value"] == "check_state"
    assert result["children"][-1]["type"] == "block"
