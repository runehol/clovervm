# Exception allocation and initializer lifecycles, cross-checked against
# CPython 3.14.5. These tests do not copy CPython test code.


def raises(error, function, /, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


class PayloadError(ValueError):
    def __init__(self, value=13, *, label="default"):
        # Args must already be set before any Python initializer runs.
        self.seen_args = self.args
        self.value = value
        self.label = label


def test_raw_arguments():
    error = PayloadError()
    assert error.args == ()
    assert error.seen_args == ()
    assert error.value == 13
    error = PayloadError(value=14, label="keyword")
    assert error.args == ()
    assert error.seen_args == ()
    assert error.value == 14
    assert error.label == "keyword"
    error = PayloadError(15, label="mixed")
    assert error.args == (15,)
    assert error.seen_args == (15,)
    assert error.label == "mixed"
    error.__init__(value=16)
    assert error.value == 16
    assert error.args == error.seen_args == (15,)
    error.__init__(17)
    assert error.value == 17
    assert error.args == error.seen_args == (15,)
    # Explicitly invoking BaseException.__init__ still updates args.
    BaseException.__init__(error, "replacement", 18)
    assert error.args == ("replacement", 18)
    raises(TypeError, PayloadError, 1, 2)
    raises(TypeError, PayloadError, 1, value=2)
    raises(TypeError, PayloadError, unknown=1)


class CapturedError(Exception):
    def __init__(self, *args, **kwargs):
        self.positional = args
        self.keywords = kwargs
        assert self.args == args


class KeywordError(Exception):
    def __init__(self, *, required):
        assert self.args == ()
        self.required = required


class CustomStop(StopIteration):
    def __init__(self, *args, **kwargs):
        self.seen_args = self.args
        self.seen_value = self.value


def test_initializer_shapes():
    error = CapturedError(1, "two", label=3)
    assert error.args == error.positional == (1, "two")
    assert len(error.keywords) == 1
    assert error.keywords["label"] == 3
    error = KeywordError(required=4)
    assert error.args == ()
    assert error.required == 4
    raises(TypeError, KeywordError)
    raises(TypeError, KeywordError, 4)
    stop = CustomStop(5, 6, ignored=7)
    assert stop.args == stop.seen_args == (5, 6)
    assert stop.value is None
    assert stop.seen_value is None
    stop.__init__(8)
    assert stop.args == (5, 6)
    assert stop.value is None
    stop = CustomStop(ignored=7)
    assert stop.args == ()
    assert stop.value is None
    assert StopIteration(9).value == 9
    assert ValueError(1, 2).args == (1, 2)
    raises(TypeError, ValueError, unexpected=1)


failure = ValueError("initializer failed")


class RaisingError(Exception):
    def __init__(self, value):
        assert self.args == (value,)
        raise failure


class ReturningError(Exception):
    def __init__(self):
        return 1


class MutableError(Exception):
    def __init__(self, value):
        self.value = value


class InheritedError(MutableError):
    pass


def changed_init(self, *, value=20):
    self.value = value


def test_failures_and_invalidation():
    assert raises(ValueError, RaisingError, 10) is failure
    raises(TypeError, ReturningError)
    for value in range(3):
        assert MutableError(value).args == (value,)
        assert InheritedError(value).args == (value,)
    MutableError.__init__ = changed_init
    error = MutableError(value=21)
    assert error.args == ()
    assert error.value == 21
    raises(TypeError, MutableError, 21)
    assert MutableError().value == 20
    inherited = InheritedError(value=23)
    assert inherited.args == ()
    assert inherited.value == 23
    assert InheritedError().value == 20
    assert PayloadError(22).args == (22,)


test_raw_arguments()
test_initializer_shapes()
test_failures_and_invalidation()
