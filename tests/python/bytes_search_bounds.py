# Expected results checked against CPython for bytes and integer needles.
# Columns: bytes, needle, start, end, find, rfind, count; then prefix/suffix
# results when the needle is bytes.
cases = (
    (b'abc', b'', 4, None, -1, -1, 0, False, False),
    (b'abc', b'', 2, 1, -1, -1, 0, False, False),
    (b'abc', b'a', 4, 99, -1, -1, 0, False, False),
    (b'abc', b'a', 2, 1, -1, -1, 0, False, False),
    (b'', b'', 1, None, -1, -1, 0, False, False),
    (b'', b'x', 0, None, -1, -1, 0, False, False),
    (b'abc', b'', 3, 3, 3, 3, 1, True, True),
    (b'abc', b'', 1, 1, 1, 1, 1, True, True),
    (b'', b'', 0, None, 0, 0, 1, True, True),
    (b'abc', b'', -1, None, 2, 3, 2, True, True),
    (b'abc', b'', -10, -20, 0, 0, 1, True, True),
    (b'abc', b'', 0, 99, 0, 3, 4, True, True),
    (b'ababa', b'a', -4, 99, 2, 4, 2, False, True),
    (b'ababa', b'ab', 0, -1, 0, 2, 2, True, True),
    (b'abc', b'b', None, None, 1, 1, 1, False, False),
    (b'abc', b'', None, None, 0, 3, 4, True, True),
    (b'abc', b'', 288230376151711743, None, -1, -1, 0, False, False),
    (b'abc', b'', -288230376151711744, 288230376151711743, 0, 3, 4, True, True),
    (b'abc', 97, 4, None, -1, -1, 0),
    (b'abc', 97, 2, 1, -1, -1, 0),
    (b'abc', 97, 1, 1, -1, -1, 0),
    (b'', 97, 0, None, -1, -1, 0),
    (b'ababa', 97, -4, 99, 2, 4, 2),
    (b'abc', 98, None, None, 1, 1, 1),
    (b'abc', 97, 288230376151711743, None, -1, -1, 0),
    (b'abc', 97, -288230376151711744, 288230376151711743, 0, 0, 1),
    (b'\x00\x01\x00', True, 0, None, 1, 1, 1),
    (b'\x00\x01\x00', False, 0, None, 0, 2, 2),
)

for case in cases:
    text = case[0]
    needle = case[1]
    start = case[2]
    end = case[3]
    assert text.find(needle, start, end) == case[4]
    assert text.rfind(needle, start, end) == case[5]
    assert text.count(needle, start, end) == case[6]
    if isinstance(needle, bytes):
        assert text.startswith(needle, start, end) == case[7]
        assert text.endswith(needle, start, end) == case[8]
    if case[4] == -1:
        raised = False
        try:
            text.index(needle, start, end)
        except ValueError:
            raised = True
        assert raised
    else:
        assert text.index(needle, start, end) == case[4]
    if case[5] == -1:
        raised = False
        try:
            text.rindex(needle, start, end)
        except ValueError:
            raised = True
        assert raised
    else:
        assert text.rindex(needle, start, end) == case[5]

# Validate integer needles before returning no match for invalid ranges.
for method in (bytes.find, bytes.rfind, bytes.count, bytes.index, bytes.rindex):
    for needle in (-1, 256, 10**30):
        raised = False
        try:
            method(b"abc", needle, 4, 1)
        except ValueError:
            raised = True
        assert raised
    raised = False
    try:
        method(b"abc", "invalid needle", 4, 1)
    except TypeError:
        raised = True
    assert raised

# End-bound validation must still run for no-match ranges.
for method in (bytes.find, bytes.rfind, bytes.count, bytes.startswith, bytes.endswith, bytes.index, bytes.rindex):
    raised = False
    try:
        method(b"abc", b"", 4, "invalid end")
    except TypeError:
        raised = True
    assert raised
