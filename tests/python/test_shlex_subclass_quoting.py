# Independently written regressions checked against CPython 3.14.5.
import shlex


class ShortString(str):
    def __len__(self):
        return 1


class HiddenString(str):
    def __getitem__(self, index):
        return "a"


class LongString(str):
    def __len__(self):
        return 1000


class NoLengthString(str):
    def __bool__(self):
        return True

    def __len__(self):
        raise AssertionError("quote must not call the length override")


class FalsyString(str):
    def __len__(self):
        return 0


# A subclass must not conceal unsafe characters or force an out-of-bounds scan.
for cls in (ShortString, HiddenString, LongString, NoLengthString):
    for text in ("a; echo injected", "a b", "a'b", "a\nb", "a\\b", "caf\u00e9"):
        value = cls(text)
        expected = shlex.quote(text)
        assert shlex.quote(value) == expected
        assert shlex.join([value]) == expected
        assert shlex.split(shlex.quote(value)) == [text]
        assert shlex.split(shlex.join([value])) == [text]

    # Safe inputs retain CPython's identity-preserving return behavior.
    safe = cls("abc_123-./@%+=:,")
    assert shlex.quote(safe) is safe
    assert shlex.join([safe]) == "abc_123-./@%+=:,"

# CPython intentionally tests truthiness before inspecting the string payload.
for text in ("a; echo injected", "safe", ""):
    value = FalsyString(text)
    assert shlex.quote(value) == "''"
    assert shlex.join([value]) == "''"
