# Python test set -- shlex module
#
# Portions adapted from CPython's Lib/test/test_shlex.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import shlex


assert shlex.__all__ == ["split", "quote", "join"]

# POSIX examples adapted from CPython's posix_data; the original upstream
# data set was from shellwords, by Hartmut Goebel.
posix_cases = (
    ("foo   bar    bla", ["foo", "bar", "bla"]),
    (r"\ x bar", [" x", "bar"]),
    (r"foo \x bar", ["foo", "x", "bar"]),
    ('blurb foo"bar"bar"fasel" baz', ["blurb", "foobarbarfasel", "baz"]),
    ("blurb foo'bar'bar'fasel' baz", ["blurb", "foobarbarfasel", "baz"]),
    ('foo "" "" "" bar', ["foo", "", "", "", "bar"]),
    ("foo '' '' '' bar", ["foo", "", "", "", "bar"]),
    (r'"foo\ bar"', [r"foo\ bar"]),
    (r'"foo\\ bar"', [r"foo\ bar"]),
    (r"'foo\ bar'", [r"foo\ bar"]),
    (r"foo\ bar", ["foo bar"]),
    (":-) ;-)", [":-)", ";-)"]),
)
for case in posix_cases:
    assert shlex.split(case[0]) == case[1]
    assert shlex.split(case[0], comments=True) == case[1]

# Distinguish every combination of split mode and comment handling.
for posix in (True, False):
    for comments in (True, False):
        assert shlex.split("", comments, posix) == []
        assert shlex.split(" \t\r\n ", comments, posix) == []
        assert shlex.split("a\vb\fc\u00a0d", comments, posix) == ["a\vb\fc\u00a0d"]
        assert shlex.split("a;b|c&&d<e>(f)", comments, posix) == ["a;b|c&&d<e>(f)"]
        assert shlex.split("x\x00y", comments, posix) == ["x\x00y"]
        assert shlex.split("caf\u00e9 \U0001f600", comments, posix) == ["caf\u00e9", "\U0001f600"]

    assert shlex.split("# comment\nnext", True, posix) == ["next"]
    assert shlex.split("# comment\rnext", True, posix) == []
    assert shlex.split("# comment\r\nnext", True, posix) == ["next"]
    assert shlex.split("a# comment", True, posix) == ["a"]
    assert shlex.split("a# comment\n b", True, posix) == ["a", "b"]
    assert shlex.split("a# comment\nb", False, posix) == ["a#", "comment", "b"]

assert shlex.split("a# comment\nb", True, True) == ["a", "b"]
assert shlex.split("a# comment\nb", True, False) == ["ab"]
assert shlex.split("a# one\n# two\nb", True, False) == ["ab"]
assert shlex.split("''# comment\nb", True) == ["", "b"]
assert shlex.split("'a#b' \"c#d\"", True) == ["a#b", "c#d"]
assert shlex.split("'a#b' \"c#d\"", True, False) == ["'a#b'", '"c#d"']
assert shlex.split("\\#x", True) == ["#x"]
assert shlex.split("\\#x", True, False) == ["\\"]

assert shlex.split("a'b c'd") == ["ab cd"]
assert shlex.split("a'b c'd", posix=False) == ["a'b", "c'd"]
assert shlex.split("'a'b", posix=False) == ["'a'", "b"]
assert shlex.split("''\"\"", posix=False) == ["''", '""']
assert shlex.split("''\"\"") == [""]
assert shlex.split("a\"unterminated", posix=False) == ['a"unterminated']
assert shlex.split("a\\", posix=False) == ["a\\"]
assert shlex.split("a\\\nb") == ["a\nb"]
assert shlex.split("a\\\nb", posix=False) == ["a\\", "b"]
assert shlex.split('"a\\$b\\`c\\nd"') == ["a\\$b\\`c\\nd"]
assert shlex.split('"a\\\nb"') == ["a\\\nb"]
assert shlex.split('"a\\\"b\\\\c"') == ['a"b\\c']
assert shlex.split('"a\\"', posix=False) == ['"a\\"']
assert shlex.split(s="one two", comments=False, posix=True) == ["one", "two"]

for posix in (True, False):
    for text in ("'", '"', "'unterminated", '"unterminated'):
        try:
            shlex.split(text, posix=posix)
            assert False
        except ValueError as error:
            assert str(error) == "No closing quotation"

for text in ("\\", "a\\", '"a\\'):
    try:
        shlex.split(text)
        assert False
    except ValueError as error:
        assert str(error) == "No escaped character"

try:
    shlex.split('"a\\"')
    assert False
except ValueError as error:
    assert str(error) == "No closing quotation"

try:
    shlex.split(None)
    assert False
except ValueError as error:
    assert str(error) == "s argument must not be None"

# Deliberate bounded-API difference from CPython's stream-capable split.
for value in (0, False, 1, [], {}, b"", b"a"):
    try:
        shlex.split(value)
        assert False
    except TypeError as error:
        assert str(error) == "s argument must be a string"


class StreamLike:
    def read(self, size):
        assert False


try:
    shlex.split(StreamLike())
    assert False
except TypeError as error:
    assert str(error) == "s argument must be a string"

# Quoting and joining examples adapted from CPython's testQuote/testJoin.
safe = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@%_-+=:,./"
assert shlex.quote(safe) == safe
assert shlex.quote(s="safe") == "safe"
for value in ("", None, 0, False, [], {}, b"", ()):
    assert shlex.quote(value) == "''"
assert shlex.quote("test file name") == "'test file name'"
assert shlex.quote("'") == "''\"'\"''"
for unsafe in ('"', "`", "$", "\\", "!", "#", ";", "|", "&", "<", ">", "(", ")",
               "*", "?", "[", "]", "~", " ", "\t", "\n", "\r", "\x00", "\u00e9", "\U0001f600"):
    assert shlex.quote("test" + unsafe + "name") == "'test" + unsafe + "name'"
    assert shlex.quote("test" + unsafe + "'name'") == "'test" + unsafe + "'\"'\"'name'\"'\"''"

for case in ((42, "int"), (True, "bool"), (b"abc", "bytes"), (["x"], "list")):
    try:
        shlex.quote(case[0])
        assert False
    except TypeError as error:
        assert str(error) == "expected string object, got '" + case[1] + "'"

for case in ((["a ", "b"], "'a ' b"), (["a", " b"], "a ' b'"),
             (["a", " ", "b"], "a ' ' b"), (['"a', 'b"'], "'\"a' 'b\"'")):
    assert shlex.join(case[0]) == case[1]
assert shlex.join([]) == ""
assert shlex.join(()) == ""
assert shlex.join(("a", "b c")) == "a 'b c'"
assert shlex.join(iter(["a", "b c"])) == "a 'b c'"
assert shlex.join(split_command=["a"]) == "a"
assert shlex.join(["a", None, 0]) == "a '' ''"
for value in (None, 1, ["a", 1], [b"abc"]):
    try:
        shlex.join(value)
        assert False
    except TypeError:
        pass

roundtrips = (
    [], [""], ["", "", ""], [safe], ["a", "b c", "d"],
    ["\u00e9", "\U0001f600", "\u00a0"],
    ["'", '"', "a'b", "'a'", "a\\b", "\\"],
    ["# comment", "$HOME", "a;b", "&&", "|", "*", "$(echo hi)"],
    ["\t", "\r", "\n", "\x00", "a\nb", "\v", "\f"],
)
for tokens in roundtrips:
    assert shlex.split(shlex.join(tokens)) == tokens
    assert shlex.split(shlex.join(tokens), comments=True) == tokens

# Public arity and keyword binding are ordinary Python function semantics.
for func in (shlex.split, shlex.quote, shlex.join):
    try:
        func()
        assert False
    except TypeError:
        pass
    try:
        func("a", unexpected=True)
        assert False
    except TypeError:
        pass
    try:
        func("a", s="b")
        assert False
    except TypeError:
        pass

try:
    shlex.split("a", False, True, False)
    assert False
except TypeError:
    pass
for func in (shlex.quote, shlex.join):
    try:
        func([], [])
        assert False
    except TypeError:
        pass
