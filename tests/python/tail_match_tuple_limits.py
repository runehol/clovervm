# CloverVM keeps the existing keywords, stored bounds, and bytes-only candidates.
assert "abc".startswith(prefix=("b",), start=1)
assert "abc".endswith(suffix=("b",), end=2)
assert b"abc".startswith(prefix=(b"b",), start=1)
assert b"abc".endswith(suffix=(b"b",), end=2)

for case in (
    ("abc".startswith, "a"),
    ("abc".endswith, "c"),
    (b"abc".startswith, b"a"),
    (b"abc".endswith, b"c"),
):
    method = case[0]
    hit = case[1]
    for candidate in (None, (None,), (), (hit,)):
        for bounds in (("bad start", None), (None, "bad end")):
            try:
                method(candidate, bounds[0], bounds[1])
            except TypeError as error:
                assert "slice indices must be integers" in str(error)
            else:
                raise AssertionError("expected a bounds error")

for case in ((b"abc".startswith, b"a"), (b"abc".endswith, b"c")):
    method = case[0]
    hit = case[1]
    mutable = bytearray(hit)
    assert method((hit, mutable))
    for candidate in (mutable, (mutable, hit), (97, hit)):
        try:
            method(candidate)
        except TypeError:
            pass
        else:
            raise AssertionError("expected bytes-only validation")
