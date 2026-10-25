# Repeated calls cross the VM's ordinary JIT tiering budget when enabled.
# Expected behavior checked against CPython 3.14.5.


def exercise():
    marker = []
    total = 0
    for repeat in range(140):
        start = 2**100 + repeat
        e = enumerate([marker, marker], start)
        a = next(e)
        b = next(e)
        assert a[0] == start and b[0] == start + 1
        assert a[1] is marker and b[1] is marker
        assert a is not b
        z = zip([marker, repeat], [repeat, marker], strict=True)
        a = next(z)
        b = next(z)
        assert a[0] is marker and a[1] == repeat
        assert b[0] == repeat and b[1] is marker
        assert a is not b
        try:
            next(z)
        except StopIteration:
            total += repeat
        else:
            raise AssertionError("expected exhaustion")
    return total


assert exercise() == 9730
