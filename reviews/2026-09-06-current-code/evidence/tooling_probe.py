import ast
import dis
import types
from pathlib import Path
source=Path('/Users/martymclean/Developer/MediaMuster/tools/avid_effects/extract.py').read_text()
def count_assert_loads(code):
    total=sum(i.opname=='LOAD_ASSERTION_ERROR' or (i.opname=='LOAD_COMMON_CONSTANT' and i.argrepr=='AssertionError') for i in dis.get_instructions(code))
    return total+sum(count_assert_loads(c) for c in code.co_consts if isinstance(c,types.CodeType))
for optimize in [0,1,2]:
    code=compile(source,'extract.py','exec',optimize=optimize)
    print(f'EXTRACTOR optimize={optimize} assertion_error_instructions={count_assert_loads(code)}')
print('Source assertion locations:',','.join(str(n.lineno) for n in ast.walk(ast.parse(source)) if isinstance(n,ast.Assert)))
