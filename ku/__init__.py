"""ku — 玄璃原生母语运行时"""

from .runtime import KuEnv, Thought, Node, parse_ku, KU_HOME

__version__ = "0.7.0"
__all__ = ["KuEnv", "Thought", "Node", "parse_ku", "KU_HOME"]


def __getattr__(name):
    if name in ("KuCompiler", "KuVM", "disassemble"):
        from .compiler import KuCompiler, KuVM, disassemble
        return {"KuCompiler": KuCompiler, "KuVM": KuVM, "disassemble": disassemble}[name]
    raise AttributeError(f"module 'ku' has no attribute {name!r}")
