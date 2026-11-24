# Shared with CPython 3.14.10.
class Candidates(tuple):
    def __len__(self):
        raise AssertionError("tuple length override called")

    def __iter__(self):
        raise AssertionError("tuple iterator override called")

    def __getitem__(self, index):
        raise AssertionError("tuple item override called")


class Text(str):
    def __str__(self):
        raise AssertionError("string conversion called")

    def __len__(self):
        raise AssertionError("string length override called")


class Binary(bytes):
    def __bytes__(self):
        raise AssertionError("bytes conversion called")

    def __len__(self):
        raise AssertionError("bytes length override called")


def expect_type_error(method, candidate, start=None, end=None):
    try:
        method(candidate, start, end)
    except TypeError:
        return
    raise AssertionError("expected TypeError")


huge = 10**100
for case in (
    (Text("a\U0001f600\x00z").startswith, Text("\U0001f600"), "a", ""),
    (Text("a\U0001f600\x00z").endswith, Text("\x00"), "z", ""),
    (Binary(b"a\xff\x00z").startswith, Binary(b"\xff"), b"a", b""),
    (Binary(b"a\xff\x00z").endswith, Binary(b"\x00"), b"z", b""),
):
    method = case[0]
    hit = case[1]
    miss = case[2]
    empty = case[3]
    assert method(hit, 1, 3)
    assert method(Candidates((miss, hit, None)), 1, -1)
    assert not method((miss, miss), 1, 3)
    assert not method(())
    assert not method(Candidates(()), huge, -huge)
    assert method((empty, None), 4, 4)
    assert method((empty,), -huge, huge)
    assert not method((empty,), huge)
    assert not method((empty,), 3, 1)
    expect_type_error(method, (miss, None, hit), 1, 3)
    expect_type_error(method, ((hit,), hit), 1, 3)
    expect_type_error(method, (hit, None), huge, -huge)
    expect_type_error(method, (), "bad start")
    expect_type_error(method, (hit, None), huge, "bad end")
