# Python test set -- json diagnostics and call conventions
#
# Portions adapted from CPython v3.14.5 Lib/test/test_json/test_fail.py
# and test_decode.py, with expected diagnostics checked using CPython 3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.

import json

# Each row is (document, message, character offset, line, column).
cases = [
    ('', 'Expecting value', 0, 1, 1),
    (' ', 'Expecting value', 1, 1, 2),
    ('[', 'Expecting value', 1, 1, 2),
    ('{', 'Expecting property name enclosed in double quotes', 1, 1, 2),
    ('[1', "Expecting ',' delimiter", 2, 1, 3),
    ('[1,', 'Expecting value', 3, 1, 4),
    ('[1,]', 'Illegal trailing comma before end of array', 2, 1, 3),
    ('[1, \n ]', 'Illegal trailing comma before end of array', 2, 1, 3),
    ('{"a":', 'Expecting value', 5, 1, 6),
    ('{"a":1', "Expecting ',' delimiter", 6, 1, 7),
    ('{"a" 1}', "Expecting ':' delimiter", 5, 1, 6),
    ('{"a":1,}', 'Illegal trailing comma before end of object', 6, 1, 7),
    ('{"a":1,\n }', 'Illegal trailing comma before end of object', 6, 1, 7),
    ('{"a":1 x}', "Expecting ',' delimiter", 7, 1, 8),
    ('{x:1}', 'Expecting property name enclosed in double quotes', 1, 1, 2),
    ('"', 'Unterminated string starting at', 0, 1, 1),
    ('"abc', 'Unterminated string starting at', 0, 1, 1),
    ('"x\\', 'Unterminated string starting at', 0, 1, 1),
    ('"\\x"', 'Invalid \\escape', 1, 1, 2),
    ('"\\u"', 'Invalid \\uXXXX escape', 2, 1, 3),
    ('"\\u12"', 'Invalid \\uXXXX escape', 2, 1, 3),
    ('"\\u1234', 'Invalid \\uXXXX escape', 2, 1, 3),
    ('"\\ud800', 'Invalid \\uXXXX escape', 2, 1, 3),
    ('"\\ud800\\udc00', 'Invalid \\uXXXX escape', 8, 1, 9),
    ('"\\ud800\\u0041', 'Invalid \\uXXXX escape', 8, 1, 9),
    ('"\\u1234x', 'Unterminated string starting at', 0, 1, 1),
    ('"\\u00xz"', 'Invalid \\uXXXX escape', 2, 1, 3),
    ('"\\ud800\\uxxxx"', 'Invalid \\uXXXX escape', 8, 1, 9),
    ('"x\nx"', 'Invalid control character at', 2, 1, 3),
    ('01', 'Extra data', 1, 1, 2),
    ('-01', 'Extra data', 2, 1, 3),
    ('1.', 'Extra data', 1, 1, 2),
    ('1e', 'Extra data', 1, 1, 2),
    ('1e+', 'Extra data', 1, 1, 2),
    ('+1', 'Expecting value', 0, 1, 1),
    ('.5', 'Expecting value', 0, 1, 1),
    ('--1', 'Expecting value', 0, 1, 1),
    ('1_0', 'Extra data', 1, 1, 2),
    ('NaN!', 'Extra data', 3, 1, 4),
    ('True', 'Expecting value', 0, 1, 1),
    ('null null', 'Extra data', 5, 1, 6),
    ('[0e]', "Expecting ',' delimiter", 2, 1, 3),
    ('[0e+]', "Expecting ',' delimiter", 2, 1, 3),
    ('[\n  1 2]', "Expecting ',' delimiter", 6, 2, 5),
    ('["abc\\y"]', 'Invalid \\escape', 5, 1, 6),
    ("['single quote']", 'Expecting value', 1, 1, 2),
    ('[1,,2]', 'Expecting value', 3, 1, 4),
    ('{"x":0x14}', "Expecting ',' delimiter", 6, 1, 7),
    ('{"x":alert()}', 'Expecting value', 5, 1, 6),
    ('{"x":1 + 2}', "Expecting ',' delimiter", 7, 1, 8),
]
cases.append((chr(65279) + "0", "Unexpected UTF-8 BOM (decode using utf-8-sig)", 0, 1, 1))
cases.append((chr(11) + "1", "Expecting value", 0, 1, 1))
cases.append(("1" + chr(160), "Extra data", 1, 1, 2))
cases.append(('"' + chr(0) + '"', "Invalid control character at", 1, 1, 2))
cases.append(('"' + chr(233) + '" x', "Extra data", 4, 1, 5))
cases.append(("1" + chr(65296), "Extra data", 1, 1, 2))
cases.append(("0." + chr(65296), "Extra data", 1, 1, 2))
cases.append(("0e" + chr(65296), "Extra data", 1, 1, 2))
for case in cases:
    try:
        json.loads(case[0])
    except json.JSONDecodeError as error:
        assert isinstance(error, ValueError)
        assert error.msg == case[1]
        assert error.doc == case[0]
        assert error.pos == case[2]
        assert error.lineno == case[3]
        assert error.colno == case[4]
        text = (case[1] + ": line " + str(case[3]) + " column " + str(case[4])
                + " (char " + str(case[2]) + ")")
        assert str(error) == text
        assert error.args == (text,)
    else:
        assert False

error = json.JSONDecodeError("problem", "a\nb", 2)
assert error.msg == "problem"
assert error.doc == "a\nb"
assert error.pos == 2
assert error.lineno == 2
assert error.colno == 1
assert str(error) == "problem: line 2 column 1 (char 2)"

# Keyword-only options and ordinary positional/keyword input arguments.
try:
    json.loads()
except TypeError:
    pass
else:
    assert False
try:
    json.loads("0", None)
except TypeError:
    pass
else:
    assert False
try:
    json.loads("0", unknown=True)
except TypeError:
    pass
else:
    assert False
try:
    json.dumps()
except TypeError:
    pass
else:
    assert False
try:
    json.dumps(0, False)
except TypeError:
    pass
else:
    assert False
try:
    json.dumps(0, unknown=True)
except TypeError:
    pass
else:
    assert False
for separators in ((), (",",), (",", ":", "!")):
    try:
        json.dumps([1, 2], separators=separators)
    except ValueError:
        pass
    else:
        assert False
assert json.loads("1", parse_int=0) == 1
assert json.loads("1.0", parse_float=False) == 1.0
for callback in (1, "invalid"):
    try:
        json.loads("1", parse_int=callback)
    except TypeError:
        pass
    else:
        assert False
    try:
        json.loads("1.0", parse_float=callback)
    except TypeError:
        pass
    else:
        assert False
    try:
        json.loads("{}", object_hook=callback)
    except TypeError:
        pass
    else:
        assert False
