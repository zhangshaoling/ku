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

def test_thought_def():
    ast = {'type':'block','children':[
        {'type':'thought','value':'add','children':['a','b',
            {'type':'block','children':[
                {'type':'return','children':[{'type':'op','value':'+',
                    'children':[{'type':'ref','value':'a'},{'type':'ref','value':'b'}]}]}
            ]}
        ]},
        {'type':'call','value':'add','children':[{'type':'literal','value':3},{'type':'literal','value':4}]}
    ]}
    assert execute(ast) == 7

def test_thought_recursive():
    ast = {'type':'block','children':[
        {'type':'thought','value':'fib','children':['n',
            {'type':'block','children':[
                {'type':'if','children':[
                    {'type':'op','value':'<=','children':[{'type':'ref','value':'n'},{'type':'literal','value':1}]},
                    {'type':'return','children':[{'type':'ref','value':'n'}]},
                    {'type':'return','children':[{'type':'op','value':'+','children':[
                        {'type':'call','value':'fib','children':[{'type':'op','value':'-','children':[{'type':'ref','value':'n'},{'type':'literal','value':1}]}]},
                        {'type':'call','value':'fib','children':[{'type':'op','value':'-','children':[{'type':'ref','value':'n'},{'type':'literal','value':2}]}]}
                    ]}]}
                ]}
            ]}
        ]},
        {'type':'call','value':'fib','children':[{'type':'literal','value':10}]}
    ]}
    assert execute(ast) == 55
