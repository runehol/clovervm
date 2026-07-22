# Python test set -- glob module
#
# Surface selected from Python 3.14's glob module help and focused local Python
# probes. This file does not copy CPython test code.

import glob


def contains(items, needle):
    for item in items:
        if item == needle:
            return True
    return False


assert glob.has_magic("stdlib/*.py")
assert glob.has_magic("stdlib/test?.py")
assert glob.has_magic("stdlib/[fg]*.py")
assert not glob.has_magic("stdlib/os.py")

assert glob.escape("a*b?[c]") == "a[*]b[?][[]c]"

literal = glob.glob("stdlib/os.py")
assert len(literal) == 1
assert literal[0] == "stdlib/os.py"

missing = glob.glob("stdlib/not-present.py")
assert len(missing) == 0

stdlib_py = glob.glob("stdlib/*.py")
assert contains(stdlib_py, "stdlib/os.py")
assert contains(stdlib_py, "stdlib/fnmatch.py")
assert contains(stdlib_py, "stdlib/glob.py")
assert not contains(stdlib_py, "stdlib/native_modules")

stdlib_filtered = glob.glob("stdlib/[fg]*.py")
assert contains(stdlib_filtered, "stdlib/fnmatch.py")
assert contains(stdlib_filtered, "stdlib/glob.py")
assert not contains(stdlib_filtered, "stdlib/os.py")

test_sources = glob.glob("tests/python/test_*.py")
assert contains(test_sources, "tests/python/test_fnmatch_module.py")
assert contains(test_sources, "tests/python/test_glob_module.py")

iterator = glob.iglob("stdlib/glob.py")
assert next(iterator) == "stdlib/glob.py"

recursive = glob.glob("stdlib/**/*.py", recursive=True)
assert contains(recursive, "stdlib/glob.py")
assert contains(recursive, "stdlib/fnmatch.py")

recursive_all = glob.glob("stdlib/**", recursive=True)
assert contains(recursive_all, "stdlib/fnmatch.py")
assert contains(recursive_all, "stdlib/native_modules")
assert contains(recursive_all, "stdlib/")
assert not contains(recursive_all, "stdlib")

assert glob.glob1("stdlib", "g*.py")[0] == "glob.py"
assert glob.glob0("stdlib", "glob.py")[0] == "glob.py"
assert len(glob.glob0("stdlib", "not-present.py")) == 0

assert glob.translate("*.py") == "(?s:(?!\\.)[^/]*\\.py)\\z"
assert glob.translate("stdlib/*.py") == "(?s:stdlib/(?!\\.)[^/]*\\.py)\\z"
assert glob.translate("a[?]b") == "(?s:a[?]b)\\z"
assert glob.__all__[0] == "glob"
assert glob.__all__[3] == "translate"
