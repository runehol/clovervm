# Self-checking builtin keyword parsing cases compared with CPython 3.14.5.
# Ordinary Python functions intentionally retain rich keyword-name comparison.


def raises_keyword(error, function, args, kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


class HostileName(str):
    def __eq__(self, other):
        raise AssertionError("keyword equality hook")

    def __ne__(self, other):
        raise AssertionError("keyword inequality hook")

    def __str__(self):
        raise AssertionError("keyword conversion hook")

    def __hash__(self):
        # Distinct explicit hashes keep dictionary construction/transport from
        # needing equality. Parsing must not look values up by canonical hash.
        return self.hash_value


def name(payload, hash_value):
    result = HostileName(payload)
    result.hash_value = hash_value
    return result


calls = []


def key(item):
    calls.append(item)
    return -item


for function in (min, max):
    calls.clear()
    assert function([1, 3, 2], **{name("key", 11): key}) == (3 if function is min else 1)
    assert calls == [1, 3, 2]
    assert function([2, 1], **{name("key", 11): None}) == (1 if function is min else 2)
    marker = []
    assert function([], **{name("default", 12): marker}) is marker
    assert function([], **{name("key", 11): 42, name("default", 12): None}) is None
    assert function(2, 1, **{name("key", 11): None}) == (1 if function is min else 2)
    raises_keyword(TypeError, function, (2, 1), {name('default', 12): None})
    raises_keyword(AssertionError, function, ([],), {name('bogus', 13): None})
    raises_keyword(TypeError, function, (), {name('bogus', 13): None})
    raises_keyword(AssertionError, function, ([],), {name('key', 11): None, name('key', 12): None})
    raises_keyword(AssertionError, function, ([],), {name('default', 11): None, name('default', 12): None})
    raises_keyword(TypeError, function, ([],), {name('key', 11): None, name('default', 12): None, name('bogus', 13): None})

calls.clear()
assert sorted([1, 3, 2], **{name("key", 11): key}) == [3, 2, 1]
assert calls == [1, 3, 2]
assert sorted([1, 3, 2], **{name("reverse", 12): True}) == [3, 2, 1]
assert sorted([1, 3, 2], **{name("key", 11): key, name("reverse", 12): True}) == [1, 2, 3]
raises_keyword(AssertionError, sorted, ([],), {name('bogus', 13): None})
raises_keyword(TypeError, sorted, (), {name('bogus', 13): None})
raises_keyword(AssertionError, sorted, ([],), {name('key', 11): None, name('key', 12): None})
raises_keyword(AssertionError, sorted, ([],), {name('reverse', 11): False, name('reverse', 12): False})
raises_keyword(TypeError, sorted, ([],), {name('key', 11): None, name('reverse', 12): False, name('bogus', 13): None})


class ForgedName(str):
    def __eq__(self, other):
        return True

    def __ne__(self, other):
        return False

    __hash__ = str.__hash__


class RejectName(str):
    def __eq__(self, other):
        return False

    def __ne__(self, other):
        return True

    __hash__ = str.__hash__


for function in (min, max, sorted):
    raises_keyword(TypeError, function, ([2, 1],), {ForgedName('bogus'): None})
    function([2, 1], **{RejectName("key"): None})


# This behavior belongs to ordinary Python call binding and must not change.
def ordinary(*, key=None):
    return key


marker = []
assert ordinary(**{ForgedName("bogus"): marker}) is marker
raises_keyword(TypeError, ordinary, (), {RejectName('key'): None})


class Source:
    def __init__(self):
        self.index = 0

    def __iter__(self):
        calls.append("iter")
        return self

    def __next__(self):
        calls.append(self.index)
        if self.index == 2:
            raise StopIteration
        self.index += 1
        return self.index


class Reverse:
    def __bool__(self):
        calls.append("reverse")
        return True


calls.clear()
raises_keyword(AssertionError, sorted, (Source(),), {name('bogus', 13): None})
assert calls == ["iter", 0, 1, 2]
calls.clear()
assert sorted(Source(), **{name("reverse", 12): Reverse()}) == [2, 1]
assert calls == ["iter", 0, 1, 2, "reverse"]
for function in (min, max):
    calls.clear()
    raises_keyword(AssertionError, function, (Source(),), {name('bogus', 13): None})
    raises_keyword(TypeError, function, (Source(), 0), {name('default', 12): None})
    assert calls == []


# Duplicate payloads may coexist when a subclass rejects equality. They must
# not be collapsed into one option by normalizing through a dictionary.
for function in (min, max, sorted):
    raises_keyword(TypeError, function, ([],), {
        RejectName("key"): None, RejectName("key"): None
    })


# Formatting an invalid name has the same callback behavior as CPython.
class BadDiagnostic(str):
    def __str__(self):
        raise RuntimeError("keyword diagnostic conversion")


for function in (min, max, sorted):
    error = raises_keyword(RuntimeError, function, ([],), {BadDiagnostic("bogus"): None})
    assert str(error) == "keyword diagnostic conversion"


# sorted must not hash the keyword object again after consuming its source.
consumed = False


class HashBeforeConsumption(str):
    def __hash__(self):
        if consumed:
            raise AssertionError("keyword rehashed after consumption")
        return 42


class ConsumptionSource:
    def __iter__(self):
        return self

    def __next__(self):
        global consumed
        consumed = True
        raise StopIteration


assert sorted(ConsumptionSource(), **{HashBeforeConsumption("key"): None}) == []


# Error formatting consumes the __str__ result's payload, without arithmetic
# dispatch through an overridden __radd__ on a returned string subclass.
class DiagnosticText(str):
    def __radd__(self, other):
        raise AssertionError("diagnostic result addition hook")


class DiagnosticName(str):
    def __str__(self):
        return DiagnosticText("shown name")


for function in (min, max, sorted):
    error = raises_keyword(TypeError, function, ([],), {DiagnosticName("bogus"): None})
    assert "shown name" in str(error)
