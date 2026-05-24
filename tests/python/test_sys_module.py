# Python test set -- sys module
#
# Surface selected from Python 3.14's built-in sys module help and CPython
# bootstrap behavior. This file does not copy CPython test code.

import sys


assert sys.modules["sys"] is sys
assert sys.modules["builtins"].__name__ == "builtins"
assert sys.path[0] == ""

assert sys.argv[0] == ""
assert sys.orig_argv[0] == ""
assert len(sys.warnoptions) == 0
assert sys.builtin_module_names[0] == "builtins"
assert sys.builtin_module_names[1] == "sys"

assert sys.byteorder == "little" or sys.byteorder == "big"
assert sys.dont_write_bytecode
assert sys.maxsize > 0
assert sys.maxunicode == 1114111
assert (
    sys.platform == "darwin"
    or sys.platform == "linux"
    or sys.platform == "win32"
    or sys.platform == "unknown"
)
assert sys.version == "0.0.0 (clovervm)"
assert sys.version_info[0] == 0
assert sys.version_info[1] == 0
assert sys.version_info[2] == 0
assert sys.version_info[3] == "alpha"
assert sys.version_info[4] == 0

assert sys.implementation.name == "clovervm"
assert sys.implementation.version is sys.version_info
assert sys.implementation.hexversion == sys.hexversion
assert sys.implementation.cache_tag is None

assert sys.getdefaultencoding() == "utf-8"
assert sys.getfilesystemencoding() == "utf-8"
assert sys.getfilesystemencodeerrors() == "surrogateescape"
assert not sys.is_finalizing()
assert sys.gettrace() is None
assert sys.getprofile() is None

old_recursion_limit = sys.getrecursionlimit()
sys.setrecursionlimit(321)
assert sys.getrecursionlimit() == 321
sys.setrecursionlimit(old_recursion_limit)

old_switch_interval = sys.getswitchinterval()
sys.setswitchinterval(0.01)
assert sys.getswitchinterval() == 0.01
sys.setswitchinterval(old_switch_interval)

assert sys.getsizeof(sys, 123) == 123

try:
    sys.setrecursionlimit(0)
    assert False
except ValueError:
    pass

try:
    sys.setswitchinterval(0.0)
    assert False
except ValueError:
    pass

try:
    sys.getsizeof(sys)
    assert False
except TypeError:
    pass
