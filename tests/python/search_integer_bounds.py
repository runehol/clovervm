# Shared str/bytes search bounds, checked against CPython 3.14.5.
# All huge values map to a small equivalent bound for these short sequences.
huge = 10**100
bounds = (
    (None, None), (False, False), (True, True),
    (-huge, -100), (-2**63, -100), (-2**58, -100),
    (-2**58 - 1, -100), (-100, -100), (-6, -6), (-3, -3),
    (-1, -1), (0, 0), (1, 1), (3, 3), (5, 5), (6, 6),
    (2**58 - 1, 100), (2**58, 100), (2**63 - 1, 100),
    (2**63, 100), (huge, 100),
)

class IntBound(int):
    def __index__(self):
        raise AssertionError("int subclass __index__ called")

    def __int__(self):
        raise AssertionError("int subclass __int__ called")

    def __lt__(self, other):
        raise AssertionError("int subclass comparison called")

    def __add__(self, other):
        raise AssertionError("int subclass arithmetic called")

# Both heap integers within the SMI range and huge subclass payloads.
bounds = bounds + ((IntBound(-huge), -100), (IntBound(huge), 100),
                   (IntBound(-5), -5), (IntBound(5), 5),
                   (IntBound(-1), -1), (IntBound(1), 1), (IntBound(0), 0))

class Text(str):
    pass

class Data(bytes):
    pass

for text in ("", "ababa", "a\u00e9\U0001f600\u00e9a", Text("ababa"),
             b"", b"ababa", Data(b"ababa")):
    if isinstance(text, str):
        needles = ("", "a", "ab", "aba", "z", "\u00e9", "\U0001f600")
        cls = str
    else:
        needles = (b"", b"a", b"ab", b"aba", b"z", 97)
        cls = bytes
    for needle in needles:
        methods = (cls.find, cls.rfind, cls.count, cls.index, cls.rindex)
        if not isinstance(needle, int):
            methods = methods + (cls.startswith, cls.endswith)
        for start_pair in bounds:
            for end_pair in bounds:
                start = start_pair[0]
                small_start = start_pair[1]
                end = end_pair[0]
                small_end = end_pair[1]
                for method in methods:
                    try:
                        expected = method(text, needle, small_start, small_end)
                    except ValueError:
                        raised = False
                        try:
                            method(text, needle, start, end)
                        except ValueError:
                            raised = True
                        assert raised
                    else:
                        assert method(text, needle, start, end) == expected

# Omitted bounds still have the same meaning as explicit None.
for pair in (("ababa", "a"), (b"ababa", b"a")):
    text = pair[0]
    needle = pair[1]
    cls = str if isinstance(text, str) else bytes
    for method in (cls.find, cls.rfind, cls.count, cls.index, cls.rindex,
                   cls.startswith, cls.endswith):
        assert method(text, needle) == method(text, needle, None, None)
        assert method(text, needle, -huge) == method(text, needle, -huge, None)
        for invalid in (1.5, "1", [], object()):
            for range_pair in ((invalid, huge), (huge, invalid),
                               (invalid, -huge), (-huge, invalid)):
                raised = False
                try:
                    method(text, needle, range_pair[0], range_pair[1])
                except TypeError:
                    raised = True
                assert raised

# Overrides can forward the original bigint to the builtin without clipping
# it at their caller (as JSONDecodeError's doc.count/rfind dispatch requires).
seen = []
class DiagnosticText(str):
    def count(self, needle, start, end):
        seen.append(end)
        return str.count(self, needle, start, end)

    def rfind(self, needle, start, end):
        seen.append(end)
        return str.rfind(self, needle, start, end)

text = DiagnosticText("a\nb\n")
for case in ((huge, 2, 3), (-huge, 0, -1)):
    pos = case[0]
    seen = []
    assert text.count("\n", 0, pos) == case[1]
    assert text.rfind("\n", 0, pos) == case[2]
    assert seen[0] is pos
    assert seen[1] is pos
