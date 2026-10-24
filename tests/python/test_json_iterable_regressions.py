# JSON iterable and callback ordering regressions, compared with CPython 3.14.5.
# These are original tests, not adapted from CPython's test suite.
import json


class Unsupported:
    pass


class Sequence(list):
    pass


obj = Unsupported()
seq = Sequence([obj, 2, 3])


def shrink(value):
    seq.pop(1)
    return 1


def append(value):
    seq.append(3)
    return 1


assert json.dumps(seq, default=shrink) == '[1, 2, 3]'
assert seq[1] == 3
seq = [obj, 2, 3]
assert json.dumps(seq, default=shrink) == '[1, 3]'
seq = Sequence([obj, 2])
assert json.dumps(seq, default=append) == '[1, 2]'
seq = [obj, 2]
assert json.dumps(seq, default=append) == '[1, 2, 3]'


class IteratedSequence(list):
    def __iter__(self):
        return iter(seq)


seq = [obj, 2]
assert json.dumps(IteratedSequence([99]), default=append) == '[1, 2]'
# The snapshot must retain ancestry against the original subclass object.
seq = Sequence([])
seq.append(seq)
try:
    json.dumps(seq)
except ValueError as error:
    assert str(error) == "Circular reference detected"
else:
    assert False

# Materialization precedes cycle checks, including a second empty snapshot.
iterations = []


class ChangingSequence(list):
    def __iter__(self):
        iterations.append(1)
        if len(iterations) == 1:
            return iter([self])
        return iter([])


assert json.dumps(ChangingSequence()) == '[[]]'
assert len(iterations) == 2


class Separator(str):
    def __getitem__(self, key):
        raise AssertionError("must not use overridden indexing")

    def __len__(self):
        raise AssertionError("must not use overridden length")


assert json.dumps([1, {"a": 2}], separators=Separator(",:")) == '[1,{"a":2}]'


class IteratedSeparator(Separator):
    def __iter__(self):
        return iter((" | ", " = "))


assert json.dumps([1, {"a": 2}], separators=IteratedSeparator("ignored")) == (
    '[1 | {"a" = 2}]'
)


class EndlessSeparators:
    def __init__(self):
        self.calls = 0

    def __iter__(self):
        return self

    def __next__(self):
        self.calls += 1
        return ","


separators = EndlessSeparators()
try:
    json.dumps([1, 2], separators=separators)
except ValueError:
    assert separators.calls == 3
else:
    assert False


class Mapping(dict):
    def items(self):
        return [("a", obj), ("z",)]


calls = []


def fail(value):
    calls.append(value)
    raise KeyError("callback")


def replace(value):
    calls.append(value)
    return 1


for sorting in (False, True):
    before = len(calls)
    try:
        json.dumps(Mapping({"real": 1}), sort_keys=sorting, default=fail)
    except KeyError as error:
        assert error.args == ("callback",)
        assert len(calls) == before + 1
        assert calls[len(calls) - 1] is obj
    else:
        assert False
    before = len(calls)
    try:
        json.dumps(Mapping({"real": 1}), sort_keys=sorting, default=replace)
    except ValueError as error:
        assert str(error) == "items must return 2-tuples"
        assert len(calls) == before + 1
    else:
        assert False


class EmptyRow(dict):
    def items(self):
        return [("a", obj), ()]


before = len(calls)
try:
    json.dumps(EmptyRow({"real": 1}), sort_keys=True, default=fail)
except ValueError as error:
    assert str(error) == "items must return 2-tuples"
    assert len(calls) == before
else:
    assert False


class IncomparableRow(dict):
    def items(self):
        return [("a", obj), 7]


try:
    json.dumps(IncomparableRow({"real": 1}), sort_keys=True, default=fail)
except TypeError:
    assert len(calls) == before
else:
    assert False
