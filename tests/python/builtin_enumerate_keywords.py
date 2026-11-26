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
# Expanded preparation gives exact keyword names to each constructor path.
kwargs = {Key("iterable"): [9]}
for repeat in range(3):
    assert list(enumerate(**kwargs)) == [(0, 9)]
    assert list(E(**kwargs)) == [(0, 9)]
    assert list(enumerate.__new__(enumerate, **kwargs)) == [(0, 9)]
    assert list(enumerate.__new__(E, **kwargs)) == [(0, 9)]
    assert list(E(iterable=[9], start=2)) == [(2, 9)]
    assert list(enumerate.__new__(E, iterable=[9], start=3)) == [(3, 9)]

kwargs = {Key("start"): 4}
assert list(enumerate([8], **kwargs)) == [(4, 8)]
assert list(E([8], **kwargs)) == [(4, 8)]
assert list(enumerate.__new__(enumerate, [8], **kwargs)) == [(4, 8)]

calls = []


class Diagnostic(str):
    def __str__(self):
        calls.append("str")
        raise ValueError("diagnostic hook")


# The normalized keyword name does not call subclass diagnostic hooks.
kwargs = {Diagnostic("iterable"): [8]}
raises(TypeError, enumerate, ([9],), kwargs)
assert calls == []
for case in ((E, ([9],)),
             (enumerate.__new__, (enumerate, [9])),
             (enumerate.__new__, (E, [9]))):
    raises(TypeError, case[0], case[1], kwargs)
assert calls == []

# Name normalization also bypasses keyword equality overrides.
class Reject(str):
    __hash__ = str.__hash__

    def __eq__(self, other):
        calls.append("eq")
        raise ValueError("keyword equality")


kwargs = {Reject("iterable"): [7]}
assert list(enumerate(**kwargs)) == [(0, 7)]
assert calls == []
assert list(E(**kwargs)) == [(0, 7)]
assert calls == []

# Warm subclass constructor caches must still honor a new __new__ binding.
def replacement(cls, /, *args, **kwargs):
    return "replacement"


E.__new__ = replacement
assert E() == "replacement"
assert next(enumerate([6])) == (0, 6)
