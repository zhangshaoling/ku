"""道 — 天书原生母语运行时"""

from .runtime import DaoEnv, Thought, Node, parse_道, DAO_HOME

__version__ = "2.0.0"
__all__ = ["DaoEnv", "Thought", "Node", "parse_道", "DAO_HOME", "ReactLoop", "TaskPlanner", "ContextManager", "MemorySystem", "ToolSystem", "SelfCorrectionEngine", "LLMAdapter"]


def __getattr__(name):
    if name in ("DaoCompiler", "DaoVM", "disassemble"):
        from .compiler import DaoCompiler, DaoVM, disassemble
        return {"DaoCompiler": DaoCompiler, "DaoVM": DaoVM, "disassemble": disassemble}[name]
    raise AttributeError(f"module 'dao' has no attribute {name!r}")



