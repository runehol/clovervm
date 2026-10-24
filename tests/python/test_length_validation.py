# Length and truth conversion share integer payload validation. The reference
# cases above the bounded-protocol section also run on CPython 3.14.5.


class Length:
    def __len__(self):
        return payload


class LengthInt(int):
    def forbidden(self, *args):
        raise AssertionError("length integer hook invoked")

    __int__ = forbidden
    __index__ = forbidden
    __lt__ = forbidden
    __gt__ = forbidden
    __bool__ = forbidden


def length(value):
    return len(value)


def truth(value):
    return not not value


def branch(value):
    if value:
        return True
    return False


def choose_or(value):
    return value or fallback


def choose_and(value):
    return value and fallback


fallback = object()
obj = Length()
for payload in (False, True, 0, 1, 2**60, 2**63 - 1,
                LengthInt(0), LengthInt(2), LengthInt(2**63 - 1)):
    # Explicitly bypass subclass hooks when constructing expected results.
    expected = int.__index__(payload)
    for repeat in range(4):
        result = length(obj)
        assert result.__class__ is int
        assert result == expected
        expected_truth = expected != 0
        assert truth(obj) is expected_truth
        assert branch(obj) is expected_truth
        if expected_truth:
            assert choose_or(obj) is obj
            assert choose_and(obj) is fallback
        else:
            assert choose_or(obj) is fallback
            assert choose_and(obj) is obj

for case in ((-1, ValueError), (-(2**100), ValueError),
             (LengthInt(-1), ValueError),
             (2**63, OverflowError), (2**100, OverflowError),
             (LengthInt(2**63), OverflowError),
             (1.5, TypeError), (None, TypeError),
             (NotImplemented, TypeError), ("2", TypeError)):
    payload = case[0]
    error = case[1]
    for convert in (length, truth, branch, choose_or, choose_and):
        for repeat in range(4):
            try:
                convert(obj)
            except error:
                pass
            else:
                raise AssertionError("invalid length accepted")

# Special methods use the class, ignoring instance attributes and __getattr__.
def instance_length():
    return -1


payload = 3
obj.__len__ = instance_length
assert length(obj) == 3
assert truth(obj) is True


def fake_length():
    return 2


class Missing:
    def __getattr__(self, name):
        return fake_length


try:
    length(Missing())
except TypeError:
    pass
else:
    raise AssertionError("ordinary attribute fallback used")
assert truth(Missing()) is True

# Neither callback exceptions nor invalid __bool__ results fall back to __len__.
failure = RuntimeError("callback identity")
events = []


class Raises:
    def __len__(self):
        events.append("len")
        raise failure


raises = Raises()
for convert in (length, truth, branch, choose_or, choose_and):
    events.clear()
    try:
        convert(raises)
    except RuntimeError as caught:
        assert caught is failure
    else:
        raise AssertionError("callback exception swallowed")
    assert events == ["len"]


class Both:
    def __bool__(self):
        events.append("bool")
        return bool_payload

    def __len__(self):
        raise AssertionError("unexpected len fallback")


both = Both()
for bool_payload in (True, False, 0, 1, None, NotImplemented):
    for convert in (truth, branch, choose_or, choose_and):
        events.clear()
        try:
            convert(both)
        except TypeError:
            assert bool_payload is not True and bool_payload is not False
        else:
            assert bool_payload is True or bool_payload is False
        assert events == ["bool"]


def raises_bool(self):
    raise failure


Both.__bool__ = raises_bool
try:
    truth(both)
except RuntimeError as caught:
    assert caught is failure
else:
    raise AssertionError("bool callback exception swallowed")

# Reuse warmed call sites across class mutations and deletion.
def zero_length(self):
    return 0


def negative_length(self):
    return -1


def payload_length(self):
    return payload


payload = 1
assert length(obj) == 1
assert truth(obj) is True
Length.__len__ = zero_length
assert length(obj) == 0
assert truth(obj) is False
Length.__len__ = negative_length
for convert in (length, truth, branch):
    try:
        convert(obj)
    except ValueError:
        pass
    else:
        raise AssertionError("stale length method")
del Length.__len__
try:
    length(obj)
except TypeError:
    pass
else:
    raise AssertionError("deleted length method used")
assert truth(obj) is True

# Bounded-protocol section: custom __index__ results remain unsupported.
class Index:
    def __index__(self):
        raise AssertionError("custom index dispatch is outside this slice")


payload = Index()
Length.__len__ = payload_length
for convert in (length, truth, branch):
    try:
        convert(obj)
    except TypeError:
        pass
    else:
        raise AssertionError("custom index result accepted")
