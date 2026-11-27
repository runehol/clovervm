# Python test set -- pprint module

import sys
from pprint import PrettyPrinter, pformat, pprint


value = {"bb": [1, 2, 3], "a": (4,)}
assert pformat(object=value) == "{'a': (4,), 'bb': [1, 2, 3]}"
assert pformat(value, sort_dicts=False) == "{'bb': [1, 2, 3], 'a': (4,)}"
assert pformat(value, indent=2, width=12) == (
    "{ 'a': (4,),\n  'bb': [ 1,\n          2,\n          3]}"
)
assert pformat([1, 2, 3], 0, 4) == "[1,\n2,\n3]"
assert pformat([10, 20], width=-1) == "[10,\n 20]"
assert pformat([1, 2, 3, 4, 5], width=10, compact=True) == "[1, 2, 3,\n 4, 5]"
assert pformat([[], {}, (), (1,), {2: 3}], depth=1) == "[[], {}, (), (...,), {...}]"
assert pformat([[1, 2], [3]], depth=1, width=1) == "[[1,\n  2],\n [3]]"
assert pformat([10, 20], width="8") == "[10, 20]"
assert pformat("alpha beta gamma", width=1) == repr("alpha beta gamma")
assert pformat({2: "two", 1.5: "middle", 1: "one"}) == (
    "{1: 'one', 1.5: 'middle', 2: 'two'}"
)
assert pformat({1: 2, "x": 3}, sort_dicts=False) == "{1: 2, 'x': 3}"
try:
    pformat({1: 2, "x": 3})
    assert False
except TypeError:
    pass

for kwargs in ({"indent": -1}, {"depth": 0}, {"width": 0}):
    try:
        PrettyPrinter(**kwargs)
        assert False
    except ValueError:
        pass
try:
    pformat([1], 1, 80, None, True)
    assert False
except TypeError:
    pass


class Inherited(list):
    def __eq__(self, other):
        raise AssertionError("ancestor search called equality")


class InheritedTuple(tuple):
    pass


class InheritedDict(dict):
    pass


class Opaque(dict):
    def __repr__(self):
        return "<custom mapping representation>"

    def items(self):
        raise AssertionError("custom repr was bypassed")


class BadRepr:
    def __repr__(self):
        return 42


class ReprText(str):
    def __str__(self):
        return "converted"


class ReprWithSubclass:
    def __repr__(self):
        return ReprText("payload")


class KeyText(str):
    def __str__(self):
        return self

    def __add__(self, other):
        raise AssertionError("dict layout called addition on a repr result")


class ReprKey:
    def __hash__(self):
        return 1

    def __repr__(self):
        return KeyText("rendered")


assert pformat(Inherited([10, 20]), width=5) == "[10,\n 20]"
assert pformat(InheritedDict({"z": InheritedTuple((1,)), "a": (2,)})) == (
    "{'a': (2,), 'z': (1,)}"
)
assert pformat(Opaque({"ignored": 1}), width=1) == "<custom mapping representation>"
assert pformat(ReprWithSubclass()) == "payload"
assert pformat([ReprWithSubclass()]) == "[payload]"
assert pformat({"key": ReprWithSubclass()}) == "{'key': converted}"
assert pformat({ReprKey(): 1}) == "{rendered: 1}"
try:
    pformat(BadRepr())
    assert False
except TypeError:
    pass

loop = []
loop.append(loop)
assert pformat(loop) == "[<Recursion on list>]"
assert pformat(loop, depth=1) == "[[...]]"
assert pformat(loop, depth=1, width=1) == "[<Recursion on list>]"
mapping = {}
wrapped = (mapping,)
mapping["back"] = wrapped
assert pformat(wrapped) == "({'back': <Recursion on tuple>},)"
shared = Inherited([1])
assert pformat([shared, shared]) == "[[1], [1]]"
outer = Inherited([Inherited([1])])
assert pformat(outer) == "[[1]]"


class Sink:
    def __init__(self, events):
        self.events = events

    def __bool__(self):
        raise AssertionError("stream truth was checked")

    def write(self, text):
        self.events.append(text)


class Traced:
    def __repr__(self):
        events.append("repr")
        return "long_leaf"


events = []
assert pprint([Traced(), Traced()], Sink(events), width=12) is None
assert events == [
    "repr", "repr", "[", "", "repr", "long_leaf", ",\n ",
    "repr", "long_leaf", "]", "\n",
]


class FailingSink(Sink):
    def write(self, text):
        self.events.append(text)
        if text == ",\n ":
            assert printer.pformat(stream_target) == "[10,\n 20]"
            raise OSError("stop")


partial = []
stream_target = [10, 20]
printer = PrettyPrinter(width=1, stream=FailingSink(partial))
try:
    printer.pprint(stream_target)
    assert False
except OSError:
    pass
assert partial == ["[", "", "10", ",\n "]
assert printer.pformat(stream_target) == "[10,\n 20]"


class Reentrant:
    def __init__(self):
        self.first = True

    def __repr__(self):
        if self.first:
            self.first = False
            assert reentrant_printer.pformat(target) == "[ok]"
            raise ValueError("first repr")
        return "ok"


reentrant_printer = PrettyPrinter()
target = [Reentrant()]
try:
    reentrant_printer.pformat(target)
    assert False
except ValueError:
    pass
assert reentrant_printer.pformat(target) == "[ok]"

missing = []
previous = getattr(sys, "stdout", missing)
before = []
after = []
try:
    sys.stdout = Sink(before)
    captured = PrettyPrinter()
    sys.stdout = Sink(after)
    captured.pprint([1])
    pprint([2], stream=None)
    sys.stdout = None
    pprint(BadRepr())
    assert pformat([3]) == "[3]"
finally:
    if previous is missing:
        del sys.stdout
    else:
        sys.stdout = previous
assert before == ["[1]", "\n"]
assert after == ["[2]", "\n"]
