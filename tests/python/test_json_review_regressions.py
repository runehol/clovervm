# JSON review regressions, checked against CPython 3.14.5.
# These are original comparison cases, not copied from CPython's tests.
import json


class Unsupported:
    pass


obj = Unsupported()
source = {"a": obj, "b": 2}


def replace_later(value):
    source["b"] = 99
    source["c"] = 3
    return 1


assert json.dumps(source, sort_keys=True, default=replace_later) == '{"a": 1, "b": 2}'
assert source["b"] == 99
assert source["c"] == 3
source = {"a": obj, "b": 2}


def delete_later(value):
    del source["b"]
    return 1


assert json.dumps(source, sort_keys=True, default=delete_later) == '{"a": 1, "b": 2}'
# The snapshot must preserve cycle detection against the original dictionary.
source = {"a": obj}


def original_dict(value):
    return source


try:
    json.dumps(source, sort_keys=True, default=original_dict)
except ValueError as error:
    assert str(error) == "Circular reference detected"
else:
    assert False


class MisleadingFloat(float):
    def __ne__(self, other):
        raise AssertionError("must not call float subclass comparisons")

    def __eq__(self, other):
        raise AssertionError("must not call float subclass comparisons")

    def __float__(self):
        raise AssertionError("must not call overridden conversion")

    def __repr__(self):
        return "misleading"


assert json.dumps(MisleadingFloat(1.5)) == "1.5"
assert json.dumps(MisleadingFloat(-0.0), allow_nan=False) == "-0.0"
assert json.dumps(MisleadingFloat(float("nan"))) == "NaN"
assert json.dumps(MisleadingFloat(float("inf"))) == "Infinity"
assert json.dumps(MisleadingFloat(float("-inf"))) == "-Infinity"
for number in (float("nan"), float("inf"), float("-inf")):
    try:
        json.dumps(MisleadingFloat(number), allow_nan=False)
    except ValueError:
        pass
    else:
        assert False


class MisleadingString(str):
    def __len__(self):
        raise AssertionError("must not call string subclass length")

    def __getitem__(self, key):
        raise AssertionError("must not call string subclass indexing")

    def __str__(self):
        raise AssertionError("must not call overridden conversion")


text = MisleadingString('a"\\' + chr(233) + chr(128512))
assert json.dumps(text) == '"a\\\"\\\\\\u00e9\\ud83d\\ude00"'
assert json.dumps(text, ensure_ascii=False) == '"a\\\"\\\\' + chr(233) + chr(128512) + '"'
assert json.dumps({text: 1}) == '{"a\\\"\\\\\\u00e9\\ud83d\\ude00": 1}'

calls = []


def callback(value):
    calls.append(value)
    return 0


for separators in ((None, ":"), (",", 2), ([], ":"), (",", None)):
    for value in (1, [], {}, obj, [obj]):
        try:
            json.dumps(value, separators=separators, default=callback)
        except TypeError:
            pass
        else:
            assert False
assert len(calls) == 0

# CPython bounds the scanned prefix, but retains the original supplied pos.
for case in (("", 1, 1, 2), ("a", 2, 1, 3), ("a\nb", 9, 2, 8),
             ("a\nb", -1, 2, -2), ("a\nb", -2, 1, -1), ("a\nb", -4, 1, -3)):
    error = json.JSONDecodeError("problem", case[0], case[1])
    assert error.pos == case[1]
    assert error.doc == case[0]
    assert error.msg == "problem"
    assert error.lineno == case[2]
    assert error.colno == case[3]
    assert str(error) == (
        "problem: line " + str(case[2]) + " column " + str(case[3])
        + " (char " + str(case[1]) + ")"
    )


class InputIndexOverride(str):
    def __getitem__(self, index):
        raise AssertionError("decoder must read the builtin string payload")


input_doc = InputIndexOverride(' ["abc", 42, true] ')
assert json.loads(input_doc) == ["abc", 42, True]
malformed = InputIndexOverride('[1,]')
try:
    json.loads(malformed)
except json.JSONDecodeError as error:
    assert error.doc is malformed
    assert error.pos == 2
    assert error.lineno == 1
    assert error.colno == 3
else:
    assert False

# Preserve the public wrapper's CPython length check, rather than normalizing
# away every subclass operation when bypassing indexing in the parser.
class InputLengthOverride(str):
    def __len__(self):
        return 0


try:
    json.loads(InputLengthOverride('"abc"'))
except json.JSONDecodeError as error:
    assert error.msg == "Extra data"
    assert error.pos == 5
else:
    assert False

huge = 10**30
for pos in (huge, -huge):
    error = json.JSONDecodeError("problem", "a\nb", pos)
    assert error.pos == pos
    if pos > 0:
        assert error.lineno == 2
        assert error.colno == pos - 1
    else:
        assert error.lineno == 1
        assert error.colno == pos + 1
    assert error.args == (
        "problem: line " + str(error.lineno) + " column " + str(error.colno)
        + " (char " + str(pos) + ")",
    )

# Encoder choices are frozen in CPython's setup order, including the public
# cached-encoder probe. Stateful flags must not be revisited while traversing.
flag_calls = []


class Flag:
    def __init__(self, name, value=True):
        self.name = name
        self.value = value
        self.calls = 0

    def __bool__(self):
        flag_calls.append(self.name)
        self.calls += 1
        return self.value


ascii_flag = Flag("ascii")
circular_flag = Flag("circular")
nan_flag = Flag("nan")
sort_flag = Flag("sort")
assert json.dumps(["\u00e9\u00e9\u00e9"], ensure_ascii=ascii_flag,
                  check_circular=circular_flag, allow_nan=nan_flag,
                  sort_keys=sort_flag, separators=(",", ":")) == '["\\u00e9\\u00e9\\u00e9"]'
assert flag_calls == ["ascii", "circular", "nan", "circular", "ascii", "sort", "nan"]


class ChangingFlag(Flag):
    def __bool__(self):
        self.calls += 1
        return self.calls < 3


changing = ChangingFlag("ascii")
assert json.dumps(["\u00e9\u00e9\u00e9"], ensure_ascii=changing,
                  separators=(",", ":")) == '["\\u00e9\\u00e9\\u00e9"]'
assert changing.calls == 2
# The cached encoder consumes each flag only in the wrapper's initial probe.
flag_calls = []
assert json.dumps(["\u00e9"], ensure_ascii=Flag("ascii"),
                  check_circular=Flag("circular"), allow_nan=Flag("nan"),
                  sort_keys=Flag("sort", False)) == '["\\u00e9"]'
assert flag_calls == ["ascii", "circular", "nan", "sort"]
# Root strings skip circularity and native encoder setup after that probe.
flag_calls = []
assert json.dumps("\u00e9", ensure_ascii=Flag("ascii", False),
                  check_circular=Flag("circular"), allow_nan=Flag("nan"),
                  sort_keys=Flag("sort")) == '"\u00e9"'
assert flag_calls == ["ascii", "ascii"]
# Separator unpacking still occurs for root strings, but native type checks do not.
assert json.dumps("\u00e9", separators=(None, None)) == '"\\u00e9"'

ascii_flag = Flag("ascii")
circular_flag = Flag("circular")
nan_flag = Flag("nan")
sort_flag = Flag("sort")


def change_flags(value):
    ascii_flag.value = False
    circular_flag.value = False
    nan_flag.value = False
    sort_flag.value = False
    return 0


assert json.dumps([obj, float("nan"), {"z": 1, "a": 2}, "\u00e9"],
                  default=change_flags, ensure_ascii=ascii_flag,
                  check_circular=circular_flag, allow_nan=nan_flag,
                  sort_keys=sort_flag) == '[0, NaN, {"a": 2, "z": 1}, "\\u00e9"]'
assert ascii_flag.calls == 2
assert circular_flag.calls == 2
assert nan_flag.calls == 2
assert sort_flag.calls == 1


class RaisingFlag:
    def __bool__(self):
        raise KeyError("flag")


calls = []
try:
    json.dumps(obj, default=callback, ensure_ascii=False, allow_nan=RaisingFlag())
except KeyError as error:
    assert error.args == ("flag",)
else:
    assert False
assert calls == []

# Diagnostic construction dispatches count/rfind on the original doc, even
# though parsing bypasses overridden indexing. Preserve order and arguments.
diagnostic_calls = []


class DiagnosticDoc(str):
    def count(self, needle, start, end):
        diagnostic_calls.append(("count", needle, start, end))
        return 8

    def rfind(self, needle, start, end):
        diagnostic_calls.append(("rfind", needle, start, end))
        return -4


doc = DiagnosticDoc("!")
try:
    json.loads(doc)
except json.JSONDecodeError as error:
    assert error.doc is doc
    assert error.lineno == 9
    assert error.colno == 4
    assert str(error) == "Expecting value: line 9 column 4 (char 0)"
else:
    assert False
assert diagnostic_calls == [("count", "\n", 0, 0), ("rfind", "\n", 0, 0)]
for pos in (10**30, -(10**30)):
    diagnostic_calls = []
    error = json.JSONDecodeError("problem", doc, pos)
    assert error.lineno == 9
    assert error.colno == pos + 4
    assert diagnostic_calls == [("count", "\n", 0, pos), ("rfind", "\n", 0, pos)]
    # Inherited builtin searches must also support these huge bounds.
    error = json.JSONDecodeError("problem", InputIndexOverride("a\nb"), pos)
    assert error.lineno == (2 if pos > 0 else 1)
    assert error.colno == (pos - 1 if pos > 0 else pos + 1)


class RaisingCount(DiagnosticDoc):
    def count(self, needle, start, end):
        raise KeyError("count")


class RaisingRfind(DiagnosticDoc):
    def rfind(self, needle, start, end):
        raise KeyError("rfind")


for case in ((RaisingCount("!"), "count"), (RaisingRfind("!"), "rfind")):
    doc = case[0]
    name = case[1]
    diagnostic_calls = []
    try:
        json.loads(doc)
    except KeyError as error:
        assert error.args == (name,)
    else:
        assert False
    assert len(diagnostic_calls) == (0 if name == "count" else 1)

# Instance attributes shadow inherited builtin methods too.
def instance_count(needle, start, end):
    assert end == 10**30
    return 5


def instance_rfind(needle, start, end):
    assert end == 10**30
    return -2


doc = InputIndexOverride("!")
doc.count = instance_count
doc.rfind = instance_rfind
error = json.JSONDecodeError("problem", doc, 10**30)
assert error.lineno == 6
assert error.colno == 10**30 + 2
