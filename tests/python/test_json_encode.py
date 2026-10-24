# Python test set -- json encoding
#
# Portions adapted from CPython v3.14.5 Lib/test/test_json/test_dump.py,
# test_default.py, test_float.py, test_unicode.py, test_recursion.py,
# test_encode_basestring_ascii.py, and test_separators.py.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Run unchanged with CPython as a comparison of the supported subset.

import json

assert json.dumps(None) == "null"
assert json.dumps(True) == "true"
assert json.dumps(False) == "false"
assert json.dumps([]) == "[]"
assert json.dumps(()) == "[]"
assert json.dumps({}) == "{}"
assert json.dumps({None: 1}) == '{"null": 1}'
assert json.dumps({None: 0, False: 1, 2: 2, 1.5: 3}) == (
    '{"null": 0, "false": 1, "2": 2, "1.5": 3}'
)
assert json.dumps({None: [None]}, separators=(",", ":")) == '{"null":[null]}'
assert json.loads(json.dumps({None: 1}))["null"] == 1
assert json.dumps(["foo", {"bar": ("baz", None, 1.0, 2)}]) == (
    '["foo", {"bar": ["baz", null, 1.0, 2]}]'
)
assert json.dumps({False: 1, 2: 2, 1.5: 3}) == '{"false": 1, "2": 2, "1.5": 3}'
assert json.dumps({True: 1}) == '{"true": 1}'
assert json.dumps({1: "number", "1": "string"}) == '{"1": "number", "1": "string"}'
assert json.dumps({"b": 1, "a": 2, "c": 3}, sort_keys=True) == '{"a": 2, "b": 1, "c": 3}'
assert json.dumps({10: 1, 2: 2, -1: 3}, sort_keys=True) == '{"-1": 3, "2": 2, "10": 1}'
assert json.dumps({"z": [{"b": 1, "a": 2}], "a": 0}, sort_keys=True) == (
    '{"a": 0, "z": [{"a": 2, "b": 1}]}'
)
assert json.dumps([1, {"x": [2, 3]}], separators=(",", ":")) == '[1,{"x":[2,3]}]'
assert json.dumps([1, {"x": 2}], separators=(" | ", " = ")) == '[1 | {"x" = 2}]'
assert json.dumps([1, 2], separators=[",", ":"]) == '[1,2]'
assert json.dumps([1, {"x": 2}], separators="ab") == '[1a{"x"b2}]'
assert json.dumps(obj=1, ensure_ascii=False, check_circular=False,
                  allow_nan=False, sort_keys=False, default=None) == "1"

assert json.dumps('"\\\b\f\n\r\t/') == '"\\\"\\\\\\b\\f\\n\\r\\t/"'
for code in (0, 1, 15, 31, 32, 65, 126, 127, 233, 8232, 55296, 56320,
             65535, 65536, 128512, 1114111):
    value = chr(code)
    assert json.loads(json.dumps(value)) == value
    assert json.loads(json.dumps(value, ensure_ascii=False)) == value
assert json.dumps(chr(233)) == '"\\u00e9"'
assert json.dumps(chr(127)) == '"\\u007f"'
assert json.dumps(chr(128512)) == '"\\ud83d\\ude00"'
assert json.dumps(chr(55296)) == '"\\ud800"'
assert json.dumps(chr(56320), ensure_ascii=False) == '"' + chr(56320) + '"'
assert json.dumps(chr(8232), ensure_ascii=False) == '"' + chr(8232) + '"'
assert json.dumps({chr(233): chr(128512)}, ensure_ascii=False) == (
    '{"' + chr(233) + '": "' + chr(128512) + '"}'
)

assert json.dumps(-0.0) == "-0.0"
for value in (0.0001, 0.00001, 1e15, 1e16, 1e20, 1.2345678901234567, 1617161771.7650001):
    assert json.loads(json.dumps(value)) == value
assert json.dumps(int("18446744073709551616")) == "18446744073709551616"
assert json.dumps(float("nan")) == "NaN"
assert json.dumps(float("inf")) == "Infinity"
assert json.dumps(float("-inf")) == "-Infinity"
assert json.dumps({float("inf"): 1}) == '{"Infinity": 1}'
for value in (float("nan"), float("inf"), float("-inf")):
    for obj in (value, [value], {value: 1}):
        try:
            json.dumps(obj, allow_nan=False)
        except ValueError:
            pass
        else:
            assert False


class Unsupported:
    def __hash__(self):
        return 17


calls = []


def replacement(obj):
    calls.append(obj)
    return {"value": (1, 2)}


obj = Unsupported()
assert json.dumps(obj, default=replacement) == '{"value": [1, 2]}'
assert calls[0] is obj
assert len(calls) == 1
assert json.dumps([obj, obj], default=replacement, separators=(",", ":")) == (
    '[{"value":[1,2]},{"value":[1,2]}]'
)
# default applies to unsupported values, never to object names.
before = len(calls)
try:
    json.dumps({obj: 1}, default=replacement)
except TypeError:
    assert len(calls) == before
else:
    assert False


def fail(obj):
    raise KeyError("callback")


try:
    json.dumps(obj, default=fail)
except KeyError as error:
    assert error.args == ("callback",)
else:
    assert False

for bad in (obj, {obj: 1}, {"s": 1, 2: 3}, {None: 1, "x": 2}):
    try:
        json.dumps(bad, sort_keys=True)
    except TypeError:
        pass
    else:
        assert False

# Identity, rather than equality, detects ancestors; shared children are valid.
child = []
assert json.dumps([child, child]) == "[[], []]"
a = []
a.append(a)
b = {}
b["self"] = b
c = []
d = (c,)
c.append(d)
for cyclic in (a, b, c):
    try:
        json.dumps(cyclic)
    except ValueError as error:
        assert str(error) == "Circular reference detected"
    else:
        assert False


def same_object(value):
    return value


def wrap_object(value):
    return [value]


for callback in (same_object, wrap_object):
    try:
        json.dumps(obj, default=callback)
    except ValueError as error:
        assert str(error) == "Circular reference detected"
    else:
        assert False
assert json.dumps([child, child], check_circular=False) == "[[], []]"
assert json.dumps(1) == "1"  # A failed call leaves no shared encoder state.
