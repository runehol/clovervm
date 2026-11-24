# Self-checking operator helper tests, also runnable against CPython.
import operator


def assert_type_error(call, /, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except TypeError:
        return
    assert False


def test_call_contracts():
    for factory in (operator.attrgetter, operator.itemgetter, operator.methodcaller):
        assert_type_error(factory)
        getter = factory("key")
        assert callable(getter)
        assert_type_error(getter)
        assert_type_error(getter, None, None)
        assert_type_error(getter, obj=None)
        assert_type_error(getter, None, extra=None)

    assert_type_error(operator.attrgetter, attr="key")
    assert_type_error(operator.attrgetter, "key", extra="other")
    assert_type_error(operator.attrgetter, 42)
    assert_type_error(operator.attrgetter, "key", ("other",))
    assert_type_error(operator.itemgetter, item="key")
    assert_type_error(operator.itemgetter, "key", extra="other")
    assert_type_error(operator.methodcaller, name="key")
    assert_type_error(operator.methodcaller, 42)


def test_attrgetter():
    events = []
    first = []
    second = []
    failure = ValueError("descriptor failed")

    class Leaf:
        def __init__(self, value):
            self.value = value

    class Target:
        @property
        def child(self):
            self.events.append("child")
            return self.leaves[len(self.events) - 1]

        @property
        def fail(self):
            self.events.append("fail")
            raise self.failure

    class Name(str):
        def split(self, separator):
            assert False

    target = Target()
    target.events = events
    target.leaves = (Leaf(first), Leaf(second))
    target.failure = failure
    single = operator.attrgetter(Name("child.value"))
    multiple = operator.attrgetter("child.value", "child.value")
    assert events == []
    assert single(target) is first
    events.clear()
    result = multiple(target)
    assert result.__class__ is tuple
    assert result[0] is first
    assert result[1] is second
    assert events == ["child", "child"]

    setattr(target, "", Leaf(first))
    assert operator.attrgetter(".value")(target) is first
    assert operator.attrgetter("")(target).value is first

    events.clear()
    try:
        operator.attrgetter("child.value", "fail", "child.value")(target)
    except ValueError as caught:
        assert caught is failure
    else:
        assert False
    assert events == ["child", "fail"]

    try:
        operator.attrgetter("missing.value")(target)
    except AttributeError:
        pass
    else:
        assert False


def test_itemgetter():
    events = []
    marker = []
    failure = LookupError("subscription failed")

    class Target:
        def __getitem__(self, key):
            self.events.append(key)
            if key == "fail":
                raise self.failure
            return key

    target = Target()
    target.events = events
    target.failure = failure
    lookup = operator.itemgetter(marker, 4, marker)
    assert events == []
    result = lookup(target)
    assert result.__class__ is tuple
    assert result == (marker, 4, marker)
    assert result[0] is marker and result[2] is marker
    assert events == [marker, 4, marker]
    assert events[0] is marker

    key = ("one", "key")
    assert operator.itemgetter(key)({key: marker}) is marker
    assert operator.itemgetter(-1)((0, marker)) is marker
    assert operator.itemgetter(slice(1, 3))((0, 1, 2, 3)) == (1, 2)

    events.clear()
    try:
        operator.itemgetter(1, "fail", 2)(target)
    except LookupError as caught:
        assert caught is failure
    else:
        assert False
    assert events == [1, "fail"]


def test_methodcaller():
    events = []
    marker = []
    failure = ValueError("method failed")

    class Target:
        def run(*args, **kwargs):
            args[0].events.append("original")
            return args, kwargs

        @property
        def current(self):
            self.events.append("lookup")
            return self.run

    def replacement(*args, **kwargs):
        events.append("replacement")
        assert "added" not in kwargs
        kwargs["added"] = True
        return args, kwargs

    def fail(self):
        events.append("fail")
        raise failure

    target = Target()
    target.events = events
    call = operator.methodcaller("current", marker, name=1, self=2, obj=3)
    saved = target.run
    assert events == []
    result = call(target)
    assert result[0] == (target, marker) and result[0][1] is marker
    assert result[1] == {"name": 1, "self": 2, "obj": 3}
    assert events == ["lookup", "original"]

    Target.run = replacement
    result = call(target)
    expected = ((target, marker), {"name": 1, "self": 2, "obj": 3, "added": True})
    assert result == expected
    assert saved(marker) == ((target, marker), {})
    assert events == ["lookup", "original", "lookup", "replacement", "original"]
    assert call(target) == expected

    setattr(Target, "literal.dot", replacement)
    assert operator.methodcaller("literal.dot")(target) == ((target,), {"added": True})

    Target.fail = fail
    try:
        operator.methodcaller("fail")(target)
    except ValueError as caught:
        assert caught is failure
    else:
        assert False
    assert events[-1] == "fail"

    target.run = 0
    assert_type_error(call, target)
    assert events[-1] == "lookup"
    try:
        operator.methodcaller("absent")(target)
    except AttributeError:
        pass
    else:
        assert False


test_call_contracts()
test_attrgetter()
test_itemgetter()
test_methodcaller()
