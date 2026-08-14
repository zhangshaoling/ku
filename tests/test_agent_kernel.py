import sys
from pathlib import Path
sys.path.insert(0, "bindings/python")
from dao_kernel import Thought, Agent, AgentPhase, AgentState, Toolbox, Runtime, fnv1a

def test_agent_state():
    """AgentState lifecycle."""
    state = AgentState("test goal", max_turns=10)
    assert state.goal == "test goal"
    assert state.max_turns == 10
    assert state.current_turn == 0
    assert state.completed is False
    assert state.phase == AgentPhase.THINK

    d = state.to_dict()
    assert d["goal"] == "test goal"
    assert d["turn"] == 0

    print("test_agent_state passed")


def test_agent_basic():
    """Agent runs a loop with a tool."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    agent = Agent("move robot", max_turns=3)
    agent.register_tool("robot", thought)

    dll = Path("kernel/out/cmake/bin/libdao_kernel.dll")
    with Runtime(dll) as rt:
        def stub(args, count):
            from dao_kernel import DaoValue, DAO_VALUE_I64
            return DaoValue(DAO_VALUE_I64, 0, 0)
        rt.register_host_function(fnv1a("sensor_distance"), 0, stub)
        rt.register_host_function(fnv1a("sensor_bearing"), 0, stub)
        rt.register_host_function(fnv1a("actuate"), 2, stub)

        result = agent.run(rt)

    assert agent.state.current_turn >= 1
    assert len(agent.state.history) >= 1
    assert agent.state.history[0]["thought"]["type"] == "start"

    print("test_agent_basic passed")


def test_agent_callbacks():
    """Phase callbacks fire."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    agent = Agent("test", max_turns=2)
    agent.register_tool("robot", thought)

    phases_fired = []
    for phase in AgentPhase:
        agent.on(phase, lambda d, p=phase: phases_fired.append(p))

    dll = Path("kernel/out/cmake/bin/libdao_kernel.dll")
    with Runtime(dll) as rt:
        def stub(args, count):
            from dao_kernel import DaoValue, DAO_VALUE_I64
            return DaoValue(DAO_VALUE_I64, 0, 0)
        rt.register_host_function(fnv1a("sensor_distance"), 0, stub)
        rt.register_host_function(fnv1a("sensor_bearing"), 0, stub)
        rt.register_host_function(fnv1a("actuate"), 2, stub)

        agent.run(rt)

    assert len(phases_fired) >= 4  # At least one full cycle
    assert AgentPhase.THINK in phases_fired
    assert AgentPhase.ACT in phases_fired

    print("test_agent_callbacks passed")


def test_agent_observe():
    """Agent observes tool results."""
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")

    agent = Agent("test", max_turns=1)
    agent.register_tool("robot", thought)

    dll = Path("kernel/out/cmake/bin/libdao_kernel.dll")
    with Runtime(dll) as rt:
        def stub(args, count):
            from dao_kernel import DaoValue, DAO_VALUE_I64
            return DaoValue(DAO_VALUE_I64, 0, 0)
        rt.register_host_function(fnv1a("sensor_distance"), 0, stub)
        rt.register_host_function(fnv1a("sensor_bearing"), 0, stub)
        rt.register_host_function(fnv1a("actuate"), 2, stub)

        agent.run(rt)

    # After running, the last observation should indicate success
    last_obs = agent.state.history[-1]["observation"]
    assert last_obs["success"] is True

    print("test_agent_observe passed")


def test_toolbox():
    """Toolbox loads .dao files and registers with Agent."""
    tb = Toolbox()
    module_bytes = open("demos/robot_control.dao", "rb").read()
    thought = Thought("robot_control", [], module_bytes, doc="robot")
    tb.add("robot", thought)

    agent = Agent("test", max_turns=1)
    tb.register_all(agent)
    assert "robot" in agent.tools

    print("test_toolbox passed")


def test_toolbox_from_directory():
    """Toolbox.from_directory loads .dao files."""
    tb = Toolbox.from_directory("demos")
    assert len(tb.tools) >= 1
    assert "robot_control" in tb.tools

    print("test_toolbox_from_directory passed")


if __name__ == "__main__":
    test_agent_state()
    test_agent_basic()
    test_agent_callbacks()
    test_agent_observe()
    test_toolbox()
    test_toolbox_from_directory()
    print("All agent tests passed")
