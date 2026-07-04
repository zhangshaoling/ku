"""道 vm_core -- minimal VM core.

8 core instructions: LOAD_CONST, LOAD_NAME, STORE_NAME, BINARY_OP,
UNARY_OP, JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE, CALL, RETURN
Plus: BUILD_LIST, BUILD_DICT, GET_ATTR, SET_ATTR, NOP

This VM is Python-implemented but designed to be self-hosted in 道.
First make it work, then rewrite it in 道 itself (self-bootstrapping).
"""

from typing import Any


# -- Opcodes --

LOAD_CONST    = "LOAD_CONST"
LOAD_NAME     = "LOAD_NAME"
STORE_NAME    = "STORE_NAME"
BINARY_OP     = "BINARY_OP"
UNARY_OP      = "UNARY_OP"
JUMP          = "JUMP"
JUMP_IF_FALSE = "JUMP_IF_FALSE"
JUMP_IF_TRUE  = "JUMP_IF_TRUE"
CALL          = "CALL"
RETURN        = "RETURN"
BUILD_LIST    = "BUILD_LIST"
BUILD_DICT    = "BUILD_DICT"
GET_ATTR      = "GET_ATTR"
SET_ATTR      = "SET_ATTR"
NOP           = "NOP"


# -- Signals --

class ReturnSignal(Exception):
    def __init__(self, value=None):
        self.value = value


class BreakSignal(Exception):
    pass


class ContinueSignal(Exception):
    pass


# -- Frame --

class Frame:
    __slots__ = ("instructions", "constants", "env", "stack", "pc")

    def __init__(self, instructions, constants, env):
        self.instructions = instructions
        self.constants = constants
        self.env = env
        self.stack = []
        self.pc = 0


# -- VM --

class DaoVM:
    """Minimal stack-based VM for 道 bytecode."""

    def __init__(self, env=None):
        self.env = env or {}

    def execute(self, bytecode):
        frame = Frame(
            instructions=bytecode["instructions"],
            constants=bytecode["constants"],
            env=dict(self.env),
        )
        return self._run(frame)

    def _run(self, frame):
        stack = frame.stack
        constants = frame.constants
        env = frame.env
        instrs = frame.instructions

        while frame.pc < len(instrs):
            op = instrs[frame.pc]
            opcode = op[0]
            arg = op[1] if len(op) > 1 else None

            if opcode == LOAD_CONST:
                stack.append(constants[arg])
                frame.pc += 1

            elif opcode == LOAD_NAME:
                name = arg
                if name in env:
                    stack.append(env[name])
                else:
                    raise NameError("vm: undefined " + repr(name))
                frame.pc += 1

            elif opcode == STORE_NAME:
                env[arg] = stack.pop()
                frame.pc += 1

            elif opcode == BINARY_OP:
                right = stack.pop()
                left = stack.pop()
                stack.append(_do_binop(arg, left, right))
                frame.pc += 1

            elif opcode == UNARY_OP:
                val = stack.pop()
                if arg == "not":
                    stack.append(not val)
                elif arg == "-":
                    stack.append(-val)
                else:
                    raise ValueError("vm: unknown unary " + repr(arg))
                frame.pc += 1

            elif opcode == JUMP:
                frame.pc += arg

            elif opcode == JUMP_IF_FALSE:
                val = stack.pop()
                if not val:
                    frame.pc += arg
                else:
                    frame.pc += 1

            elif opcode == JUMP_IF_TRUE:
                val = stack.pop()
                if val:
                    frame.pc += arg
                else:
                    frame.pc += 1

            elif opcode == CALL:
                argc = arg
                args = []
                for _ in range(argc):
                    args.append(stack.pop())
                args.reverse()
                func = stack.pop()
                if callable(func):
                    result = func(*args)
                    stack.append(result)
                elif isinstance(func, dict) and func.get("_type") == "thought":
                    result = _call_thought(func, args, env)
                    stack.append(result)
                else:
                    raise TypeError("vm: cannot call " + repr(func))
                frame.pc += 1

            elif opcode == RETURN:
                raise ReturnSignal(stack.pop() if stack else None)

            elif opcode == BUILD_LIST:
                items = []
                for _ in range(arg):
                    items.append(stack.pop())
                items.reverse()
                stack.append(items)
                frame.pc += 1

            elif opcode == BUILD_DICT:
                d = {}
                for _ in range(arg):
                    v = stack.pop()
                    k = stack.pop()
                    d[k] = v
                stack.append(d)
                frame.pc += 1

            elif opcode == GET_ATTR:
                obj = stack.pop()
                stack.append(getattr(obj, arg))
                frame.pc += 1

            elif opcode == SET_ATTR:
                val = stack.pop()
                obj = stack.pop()
                setattr(obj, arg, val)
                frame.pc += 1

            elif opcode == NOP:
                frame.pc += 1

            elif opcode == "REACT_THINK":
                # v2: push thought onto stack
                frame.stack.append({"type": "think", "turn": frame.pc})
                frame.pc += 1

            elif opcode == "REACT_ACT":
                # v2: execute action
                action = frame.stack.pop()
                frame.stack.append({"type": "act", "action": action})
                frame.pc += 1

            elif opcode == "REACT_OBSERVE":
                # v2: observe result
                result = frame.stack.pop()
                frame.stack.append({"type": "observe", "result": result})
                frame.pc += 1

            elif opcode == "REACT_REPLAN":
                # v2: re-plan
                frame.stack.append({"type": "replan"})
                frame.pc += 1

            elif opcode == "MEM_STORE":
                # v2: store memory
                key = frame.stack.pop()
                val = frame.stack.pop()
                _mem_store_impl(env, key, val)
                frame.pc += 1

            elif opcode == "MEM_RECALL":
                # v2: recall memory
                key = frame.stack.pop()
                frame.stack.append(_mem_recall_impl(env, key))
                frame.pc += 1

            elif opcode == "TOOL_CALL":
                # v2: call tool
                args = frame.stack.pop()
                tool_name = frame.stack.pop()
                frame.stack.append({"type": "tool_call", "tool": tool_name, "args": args})
                frame.pc += 1

            else:
                raise ValueError("vm: unknown opcode " + repr(opcode))

        return stack[-1] if stack else None


# -- Helpers --

def _do_binop(op, left, right):
    if op == "+":
        return left + right
    elif op == "-":
        return left - right
    elif op == "*":
        return left * right
    elif op == "/":
        return left / right
    elif op == "//":
        return left // right
    elif op == "%":
        return left % right
    elif op == "**":
        return left ** right
    elif op == "==":
        return left == right
    elif op == "!=":
        return left != right
    elif op == "<":
        return left < right
    elif op == "<=":
        return left <= right
    elif op == ">":
        return left > right
    elif op == ">=":
        return left >= right
    elif op == "and":
        return left and right
    elif op == "or":
        return left or right
    else:
        raise ValueError("vm: unknown binop " + repr(op))


def _call_thought(thought, args, parent_env):
    params = thought["params"]
    body = thought["body"]
    closure_env = dict(thought.get("env", {}))
    closure_env.update(parent_env)
    for i, pname in enumerate(params):
        closure_env[pname] = args[i] if i < len(args) else None
    vm = 道VM(env=closure_env)
    try:
        return vm.execute({"instructions": body, "constants": []})
    except ReturnSignal as rs:
        return rs.value


def run_bytecode(bytecode, env=None):
    vm = 道VM(env=env)
    try:
        return vm.execute(bytecode)
    except ReturnSignal as rs:
        return rs.value


if __name__ == "__main__":
    # Simple self-test
    bc = {
        "constants": [1, 2],
        "instructions": [
            (LOAD_CONST, 0),
            (LOAD_CONST, 1),
            (BINARY_OP, "+"),
            (RETURN,),
        ],
    }
    result = run_bytecode(bc)
    print("self-test result:", result)
    assert result == 3, "expected 3, got " + str(result)
    print("all tests passed")



# v2.0 VM helpers
def _mem_store_impl(env, key, val):
    if "__mem__" not in env:
        env["__mem__"] = {}
    env["__mem__"][key] = val

def _mem_recall_impl(env, key):
    return env.get("__mem__", {}).get(key)

def run_道_bytecode(bytecode, env=None):
    vm = DaoVM(env=env)
    try:
        return vm.execute(bytecode)
    except ReturnSignal as rs:
        return rs.value
