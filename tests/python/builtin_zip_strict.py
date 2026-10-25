# Zip behavior compared with CPython 3.14.5 Python/bltinmodule.c.


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


log = []


class Source:
    def __init__(self, name, values):
        self.name = name
        self.values = iter(values)

    def __iter__(self):
        log.append("iter " + self.name)
        return self

    def __next__(self):
        log.append("next " + self.name)
        value = next(self.values)
        if value == "stop":
            raise StopIteration("payload")
        if value == "error":
            raise ValueError("source failure")
        return value


class Truth:
    def __bool__(self):
        log.append("bool")
        return True


z = zip(Source("a", [1]), Source("b", [2]), strict=Truth())
assert log == ["bool", "iter a", "iter b"]
assert next(z) == (1, 2)
assert log == ["bool", "iter a", "iter b", "next a", "next b"]
log.clear()
raises(TypeError, zip, Source("a", []), strict=Truth(), invalid=0)
assert log == []

# A failed row is discarded; consumption already performed is not rolled back.
z = zip(Source("a", [1, 2, 3]), Source("b", ["error", 8, 9]))
raises(ValueError, next, z)
assert next(z) == (2, 8)
assert next(z) == (3, 9)

z = zip(Source("a", ["stop", 1]), Source("b", [7]))
assert raises(StopIteration, next, z).args == ("payload",)
assert next(z) == (1, 7)
z = zip(Source("a", ["stop", 1]), Source("b", ["stop", 7]), strict=True)
assert raises(StopIteration, next, z).args == ()
assert next(z) == (1, 7)

# Exact diagnostics include the plural range and repeated mismatch consumption.
for case in (([1], [], "zip() argument 2 is shorter than argument 1"),
             ([], [1], "zip() argument 2 is longer than argument 1")):
    z = zip(case[0], case[1], strict=True)
    assert raises(ValueError, next, z).args == (case[2],)
    raises(StopIteration, next, z)
z = zip([1], [2], [], strict=True)
assert raises(ValueError, next, z).args == ("zip() argument 3 is shorter than arguments 1-2",)
z = zip([], [], [3], strict=True)
assert raises(ValueError, next, z).args == ("zip() argument 3 is longer than arguments 1-2",)

log.clear()
z = zip(Source("a", []), Source("b", [1, 2]), Source("c", [3]), strict=True)
assert raises(ValueError, next, z).args == ("zip() argument 2 is longer than argument 1",)
assert log == ["iter a", "iter b", "iter c", "next a", "next b"]
raises(ValueError, next, z)
assert raises(ValueError, next, z).args == ("zip() argument 3 is longer than arguments 1-2",)
raises(StopIteration, next, z)

# Non-StopIteration errors in strict probes propagate and remain recoverable.
z = zip(Source("a", []), Source("b", ["error", 2]), strict=True)
assert raises(ValueError, next, z).args == ("source failure",)
assert raises(ValueError, next, z).args == ("zip() argument 2 is longer than argument 1",)
raises(StopIteration, next, z)


# A later exhausted input reports immediately, without touching following ones.
log.clear()
z = zip(Source("a", [1]), Source("b", ["stop"]), Source("c", ["error"]), strict=True)
assert raises(ValueError, next, z).args == ("zip() argument 2 is shorter than argument 1",)
assert log == ["iter a", "iter b", "iter c", "next a", "next b"]
