# Expected results checked against CPython for normalized search bounds.
# Columns: string, needle, start, end, find, rfind, count, startswith, endswith.
cases = (
    ("abc", "", 4, None, -1, -1, 0, False, False),
    ("abc", "", 2, 1, -1, -1, 0, False, False),
    ("abc", "a", 4, 99, -1, -1, 0, False, False),
    ("abc", "a", 2, 1, -1, -1, 0, False, False),
    ("", "", 1, None, -1, -1, 0, False, False),
    ("", "x", 0, None, -1, -1, 0, False, False),
    ("abc", "", 3, 3, 3, 3, 1, True, True),
    ("abc", "", 1, 1, 1, 1, 1, True, True),
    ("", "", 0, None, 0, 0, 1, True, True),
    ("abc", "", -1, None, 2, 3, 2, True, True),
    ("abc", "", -10, -20, 0, 0, 1, True, True),
    ("abc", "", 0, 99, 0, 3, 4, True, True),
    ("ababa", "a", -4, 99, 2, 4, 2, False, True),
    ("ababa", "ab", 0, -1, 0, 2, 2, True, True),
    ("abc", "b", None, None, 1, 1, 1, False, False),
    ("abc", "", None, None, 0, 3, 4, True, True),
    ("abc", "", 288230376151711743, None, -1, -1, 0, False, False),
    ("abc", "", -288230376151711744, 288230376151711743, 0, 3, 4, True, True),
)

for case in cases:
    text = case[0]
    needle = case[1]
    start = case[2]
    end = case[3]
    assert text.find(needle, start, end) == case[4]
    assert text.rfind(needle, start, end) == case[5]
    assert text.count(needle, start, end) == case[6]
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

# Argument validation still runs even when the range cannot match.
for method in (str.find, str.rfind, str.count, str.startswith, str.endswith, str.index, str.rindex):
    raised = False
    try:
        method("abc", "", 4, "invalid end")
    except TypeError:
        raised = True
    assert raised
