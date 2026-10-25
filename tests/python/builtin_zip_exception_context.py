# Exception chaining behavior compared with CPython 3.14.5.
# Use getattr because CloverVM does not expose a default __context__ slot.


def caught(kind, iterator):
    try:
        next(iterator)
    except kind as error:
        return error
    raise AssertionError("expected exception")


payload = object()


class End:
    def __init__(self):
        self.error = StopIteration(payload)
        self.inner = RuntimeError("source context")

    def __iter__(self):
        return self

    def __next__(self):
        try:
            raise self.inner
        except RuntimeError:
            raise self.error


# Both mismatch directions discard exhaustion and its user-supplied context.
for repeat in range(3):
    error = caught(ValueError, zip([1], End(), strict=True))
    assert error.args == ("zip() argument 2 is shorter than argument 1",)
    assert getattr(error, "__context__", None) is None
    error = caught(ValueError, zip(End(), [1], strict=True))
    assert error.args == ("zip() argument 2 is longer than argument 1",)
    assert getattr(error, "__context__", None) is None

error = caught(ValueError, zip([1], [2], End(), strict=True))
assert error.args == ("zip() argument 3 is shorter than arguments 1-2",)
assert getattr(error, "__context__", None) is None
error = caught(ValueError, zip(End(), End(), [3], strict=True))
assert error.args == ("zip() argument 3 is longer than arguments 1-2",)
assert getattr(error, "__context__", None) is None

# Final exhaustion is a fresh, empty StopIteration with no source chain.
for count in range(3):
    if count == 0:
        iterator = zip(strict=True)
    elif count == 1:
        iterator = zip(End(), strict=True)
    else:
        iterator = zip(End(), End(), strict=True)
    error = caught(StopIteration, iterator)
    assert error.args == ()
    assert getattr(error, "__context__", None) is None

# Non-strict exhaustion still forwards the source exception unchanged.
source = End()
error = caught(StopIteration, zip(source))
assert error is source.error
assert error.args[0] is payload
assert error.__context__ is source.inner


class Fail(End):
    def __init__(self):
        self.error = ValueError("source failure")
        self.inner = RuntimeError("user context")


# Real source errors retain their existing context, including strict probes.
for strict in (False, True):
    source = Fail()
    error = caught(ValueError, zip(source, strict=strict))
    assert error is source.error and error.__context__ is source.inner
source = Fail()
error = caught(ValueError, zip(End(), source, strict=True))
assert error is source.error and error.__context__ is source.inner
