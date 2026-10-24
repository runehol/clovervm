# Python test set -- json decoding
#
# Portions adapted from CPython v3.14.5 Lib/test/test_json/test_decode.py,
# test_float.py, test_unicode.py, and test_scanstring.py.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Run unchanged with CPython as a comparison of the supported subset.

import json


assert json.loads("null") is None
assert json.loads("true") is True
assert json.loads("false") is False
assert json.loads("[]") == []
assert json.loads("{}") == {}
assert json.loads('""') == ""
assert json.loads(" \t\r\n[1, -2, 0, -0, 1.5, 1E+2, 0.25e-1]\r\n") == [
    1, -2, 0, 0, 1.5, 100.0, 0.025
]
assert json.loads('{   "key"    :    "value"    ,  "k":"v"    }') == {
    "key": "value", "k": "v"
}
assert json.loads('[{"x":[true,null]},[]]') == [{"x": [True, None]}, []]
assert json.loads('{"x":1,"x":2,"y":3}') == {"x": 2, "y": 3}
assert json.loads('"\\\"\\\\\\/\\b\\f\\n\\r\\t"') == '"\\/\b\f\n\r\t'
assert json.loads('"\\u0000\\u001F"') == chr(0) + chr(31)
assert json.loads('"\\u00e9\\u00E9"') == chr(233) + chr(233)
assert json.loads('"\\ud83d\\ude00"') == chr(128512)
assert json.loads('"\\ud800"') == chr(55296)
assert json.loads('"\\udc00"') == chr(56320)
assert json.loads('"\\ud800x\\udc00"') == chr(55296) + "x" + chr(56320)
assert json.loads('"\\ud800\\u0041"') == chr(55296) + "A"
assert json.loads('"' + chr(128512) + chr(55296) + '"') == chr(128512) + chr(55296)
assert json.loads('"' + chr(8232) + '"') == chr(8232)
assert json.loads("123456789012345678901234567890") == int("123456789012345678901234567890")
assert json.loads("-123456789012345678901234567890") == -int("123456789012345678901234567890")
assert json.loads("23456789012E666") == float("inf")
assert json.loads("-23456789012E666") == float("-inf")
assert str(json.loads("-0.0")) == "-0.0"
assert json.loads("1e-9999") == 0.0
assert json.loads("NaN") != json.loads("NaN")
assert json.loads("Infinity") == float("inf")
assert json.loads("-Infinity") == float("-inf")

# Callbacks see complete, unmodified tokens and run for every occurrence,
# including values discarded by a duplicate key and empty nested objects.
events = []


def integer(token):
    events.append("int:" + token)
    return "i:" + token


def floating(token):
    events.append("float:" + token)
    return "f:" + token


def object_result(value):
    events.append(value)
    return [value]


result = json.loads('{"n":-0,"n":123,"a":[1.50e+02,{}]}',
                    object_hook=object_result, parse_int=integer, parse_float=floating)
assert result == [{"n": "i:123", "a": ["f:1.50e+02", [{}]]}]
assert events == ["int:-0", "int:123", "float:1.50e+02", {},
                  {"n": "i:123", "a": ["f:1.50e+02", [{}]]}]
assert json.loads("1", parse_int=float) == 1.0
assert json.loads("1.1", parse_float=integer) == "i:1.1"
assert json.loads(s="42") == 42
assert json.loads("42", object_hook=None, parse_float=None, parse_int=None) == 42


def fail(value):
    raise KeyError("callback")


for text in ("1", "1.0", "{}"):
    try:
        json.loads(text, parse_int=fail, parse_float=fail, object_hook=fail)
    except KeyError as error:
        assert error.args == ("callback",)
    else:
        assert False

for bad in (None, 1, 3.14, [], {}):
    try:
        json.loads(bad)
    except TypeError:
        pass
    else:
        assert False

# None of the ordinary-number hooks are called for non-finite constants.
assert json.loads("NaN", parse_float=fail, parse_int=fail) != 0
assert json.loads("Infinity", parse_float=fail, parse_int=fail) == float("inf")
