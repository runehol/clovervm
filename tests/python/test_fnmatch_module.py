# Python test set -- fnmatch module
#
# Surface selected from Python 3.14's fnmatch module help and focused local
# Python probes. This file does not copy CPython test code.

import fnmatch
import os


assert os.path.normcase("MiXeD") == "MiXeD"

assert fnmatch.fnmatch("main.py", "*.py")
assert fnmatch.fnmatchcase("main.py", "*.py")
assert not fnmatch.fnmatchcase("main.py", "*.txt")
assert fnmatch.fnmatchcase("ab.txt", "a?.txt")
assert not fnmatch.fnmatchcase("abc.txt", "a?.txt")
assert fnmatch.fnmatchcase(".hidden", "*")
assert fnmatch.fnmatchcase("abc", "a**c")
assert fnmatch.fnmatchcase(b"main.py", b"*.py")
assert not fnmatch.fnmatchcase(b"main.py", b"*.txt")
assert fnmatch.fnmatchcase(b"ab.txt", b"a?.txt")
assert not fnmatch.fnmatchcase(b"abc.txt", b"a?.txt")
assert fnmatch.fnmatchcase(b"abc", b"a**c")

assert fnmatch.fnmatchcase("file7.txt", "file[0-9].txt")
assert not fnmatch.fnmatchcase("filex.txt", "file[0-9].txt")
assert fnmatch.fnmatchcase("filex.txt", "file[!0-9].txt")
assert not fnmatch.fnmatchcase("file7.txt", "file[!0-9].txt")
assert fnmatch.fnmatchcase(b"file7.txt", b"file[0-9].txt")
assert not fnmatch.fnmatchcase(b"filex.txt", b"file[0-9].txt")
assert fnmatch.fnmatchcase(b"filex.txt", b"file[!0-9].txt")
assert not fnmatch.fnmatchcase(b"file7.txt", b"file[!0-9].txt")
assert fnmatch.fnmatchcase("[abc", "[[]abc")
assert fnmatch.fnmatchcase("a", "[a-]")
assert fnmatch.fnmatchcase("-", "[a-]")
assert fnmatch.fnmatchcase("a", "[-a]")
assert fnmatch.fnmatchcase("-", "[-a]")
assert not fnmatch.fnmatchcase("m", "[z-a]")
assert not fnmatch.fnmatchcase("]", "[]")
assert not fnmatch.fnmatchcase("!", "[!]")

names = ["main.py", "test.py", "README.md", "setup.cfg"]
matches = fnmatch.filter(names, "*.py")
assert len(matches) == 2
assert matches[0] == "main.py"
assert matches[1] == "test.py"

misses = fnmatch.filterfalse(names, "*.py")
assert len(misses) == 2
assert misses[0] == "README.md"
assert misses[1] == "setup.cfg"

byte_names = [b"main.py", b"test.py", b"README.md", b"setup.cfg"]
byte_matches = fnmatch.filter(byte_names, b"*.py")
assert len(byte_matches) == 2
assert byte_matches[0] == b"main.py"
assert byte_matches[1] == b"test.py"

byte_misses = fnmatch.filterfalse(byte_names, b"*.py")
assert len(byte_misses) == 2
assert byte_misses[0] == b"README.md"
assert byte_misses[1] == b"setup.cfg"

assert fnmatch.translate("*.py") == "(?s:.*\\.py)\\z"
assert fnmatch.translate("a?.txt") == "(?s:a.\\.txt)\\z"
assert fnmatch.translate("file[0-9].txt") == "(?s:file[0-9]\\.txt)\\z"
assert fnmatch.translate("file[!0-9].txt") == "(?s:file[^0-9]\\.txt)\\z"
assert fnmatch.translate("[[]abc") == "(?s:[\\[]abc)\\z"
assert fnmatch.translate("[z-a]") == "(?s:(?!))\\z"

try:
    fnmatch.fnmatchcase(b"a", "*")
    assert False
except TypeError:
    pass

try:
    fnmatch.fnmatchcase("a", b"*")
    assert False
except TypeError:
    pass

try:
    fnmatch.translate(b"*.py")
    assert False
except TypeError:
    pass

assert fnmatch.__all__[0] == "filter"
assert fnmatch.__all__[1] == "filterfalse"
assert fnmatch.__all__[4] == "translate"
