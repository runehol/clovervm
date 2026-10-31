# CloverVM-specific, agreed storage/index limitations; not a CPython oracle.
class PaddingIndex:
    def __index__(self):
        raise AssertionError("custom index protocol is excluded")

for method in (str.ljust, str.rjust, str.center, str.zfill):
    for width in (288230376151711744, 9223372036854775807):
        raised = False
        try:
            method('abc', width)
        except OverflowError:
            raised = True
        assert raised
    raised = False
    try:
        method('abc', PaddingIndex())
    except TypeError:
        raised = True
    assert raised

# Fill validation precedes the positive result-storage limit.
for method in (str.ljust, str.rjust, str.center):
    raised = False
    try:
        method('abc', 9223372036854775807, '')
    except TypeError:
        raised = True
    assert raised
