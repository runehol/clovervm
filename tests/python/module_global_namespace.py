from tests.python.support.module_global_provider import (
    read_builtin_probe,
    read_value,
)


value = 22
builtin_probe = 99

assert read_value() == 11
assert read_builtin_probe() == 31

import sys

sys.local_builtin_probe = 123
__builtins__ = sys
assert local_builtin_probe == 123

__builtins__ = 1
del __builtins__
assert len((1, 2, 3)) == 3
