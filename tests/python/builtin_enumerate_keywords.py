# Constructor keyword behavior compared with CPython 3.14.5:
# Objects/enumobject.c, Objects/clinic/enumobject.c.h, Python/getargs.c.


def raises(error, function, args, kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


class Key(str):
    def __hash__(self):
        return 43 if str.__str__(self) == "iterable" else 44


class E(enumerate):
    pass


assert hash("iterable") != 43 and hash("start") != 44
# Payload matching is specific to exact type calls, not public __new__.
kwargs = {Key("iterable"): [9]}
for repeat in range(3):
    assert list(enumerate(**kwargs)) == [(0, 9)]
    raises(TypeError, E, (), kwargs)
    raises(TypeError, enumerate.__new__, (enumerate,), kwargs)
    raises(TypeError, enumerate.__new__, (E,), kwargs)
    assert list(E(iterable=[9], start=2)) == [(2, 9)]
    assert list(enumerate.__new__(E, iterable=[9], start=3)) == [(3, 9)]

kwargs = {Key("start"): 4}
assert list(enumerate([8], **kwargs)) == [(4, 8)]
raises(TypeError, E, ([8],), kwargs)
raises(TypeError, enumerate.__new__, (enumerate, [8]), kwargs)

calls = []


class Diagnostic(str):
    def __str__(self):
        calls.append("str")
        raise ValueError("diagnostic hook")


# Duplicate validation in Clinic's parser precedes diagnostic conversion.
kwargs = {Diagnostic("iterable"): [8]}
raises(ValueError, enumerate, ([9],), kwargs)
assert calls == ["str"]
calls.clear()
for case in ((E, ([9],)),
             (enumerate.__new__, (enumerate, [9])),
             (enumerate.__new__, (E, [9]))):
    raises(TypeError, case[0], case[1], kwargs)
assert calls == []

# Lookup uses equality when the stored hash matches the canonical keyword.
class Reject(str):
    __hash__ = str.__hash__

    def __eq__(self, other):
        calls.append("eq")
        raise ValueError("keyword equality")


kwargs = {Reject("iterable"): [7]}
assert list(enumerate(**kwargs)) == [(0, 7)]
assert calls == []
raises(ValueError, E, (), kwargs)
assert calls == ["eq"]

# Warm subclass constructor caches must still honor a new __new__ binding.
def replacement(cls, /, *args, **kwargs):
    return "replacement"


E.__new__ = replacement
assert E() == "replacement"
assert next(enumerate([6])) == (0, 6)
