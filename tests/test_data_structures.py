import sys, os, pytest
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
from dao.runtime import DaoEnv, _inject_builtins, _inject_parser_to_env, Thought
from dao.compiler import DaoVM
from pathlib import Path

@pytest.fixture(scope="module", autouse=True)
def load_compiler():
    env = DaoEnv()
    _inject_builtins(env)
    _inject_parser_to_env(env)
    env.load(str(Path(__file__).parent.parent / 'dao' / 'std' / 'compiler.ku'))

def execute(ast_dict):
    bc = Thought.registry['compile_ast'].call([ast_dict])
    return DaoVM().execute(bc)

def test_list():
    ast = {'type':'list','children':[{'type':'literal','value':1},{'type':'literal','value':2},{'type':'literal','value':3}]}
    assert execute(ast) == [1, 2, 3]

def test_dict():
    ast = {'type':'dict','children':[{'type':'pair','value':'a','children':[{'type':'literal','value':1}]},{'type':'pair','value':'b','children':[{'type':'literal','value':2}]}]}
    assert execute(ast) == {'a': 1, 'b': 2}

def test_index():
    ast = {'type':'block','children':[{'type':'assign','value':'arr','children':[{'type':'list','children':[{'type':'literal','value':10},{'type':'literal','value':20}]}]},{'type':'index','children':[{'type':'ref','value':'arr'},{'type':'literal','value':1}]}]}
    assert execute(ast) == 20
