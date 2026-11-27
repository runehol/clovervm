# Stream fixtures generated with CPython 3.14.10. The first ten raw words
# are also the mt19937ar authors' init_by_array example.

import random
from random import Random


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error:
        return
    raise AssertionError("expected an exception")


key = 291 | (564 << 32) | (837 << 64) | (1110 << 96)
r = Random(key)
for word in (1067595299, 955945823, 477289528, 4107218783, 4228976476,
             3344332714, 3355579695, 227628506, 810200273, 2591290167):
    assert r.getrandbits(32) == word

# CPython 3.14.10 checkpoints for a 626-word seed. Draws 226/227
# straddle the wrapped recurrence, and 623/624 straddle the next twist.
r = Random((1 << 20031) + (1 << 19936) + (1 << 47) + 9)
checkpoints = {
    0: 2394070786,
    226: 2173056896,
    227: 660317792,
    396: 2100509598,
    397: 117152961,
    623: 1656310699,
    624: 2884961517,
    625: 3262898796,
}
for index in range(626):
    draw = r.getrandbits(32)
    expected = checkpoints.get(index)
    if expected is not None:
        assert draw == expected

seed = (1 << 100) + 17
r = Random(x=-seed)
assert r.getstate() == Random(seed).getstate()
assert r.getstate()[1][-1] == 624
assert r.getrandbits(0) == 0
assert r.getstate()[1][-1] == 624
assert r.random() == 0.44461458777686025
assert r.getrandbits(33) == 4596609720
assert r.randrange(start=40, stop=-80, step=-3) == -65
assert r.getrandbits(100) == 1169123685144677328515553730988
assert r.randrange(1 << 120) == 25243094780202072874255386820617387
assert r.getrandbits(64) == 5319768650984189603
assert r.getrandbits(65) == 2146790532285828582
assert r.getstate()[1][-1] == 22
assert Random(False).getstate() == Random(0).getstate()
assert Random(True).getstate() == Random(1).getstate()

before = r.getstate()
try:
    Random.random(self=r)
except TypeError:
    pass
else:
    raise AssertionError("random() accepted self by keyword")
assert r.getstate() == before


class Payload(int):
    def __int__(self):
        raise AssertionError("seed payload was converted")

    def __abs__(self):
        raise AssertionError("seed payload was converted")

    def __index__(self):
        raise AssertionError("integer payload was converted")


r.seed(a=Payload(-seed), version=1)
assert r.getstate() == Random(seed).getstate()
assert r.getrandbits(Payload(33)) == Random(seed).getrandbits(33)
assert r.randrange(Payload(2), Payload(3), Payload(1)) == 2

r = Random(17)
for i in range(623):
    r.getrandbits(32)
marker = object()
snapshot = (3, r.getstate()[1], marker)
assert snapshot[1][-1] == 623
a = r.random()
b = r.getrandbits(1025)
restored = Random(0)
restored.setstate(state=snapshot)
assert restored.getstate()[2] is marker
assert restored.random() == a
assert restored.getrandbits(1025) == b
assert restored.getstate()[1] == r.getstate()[1]

legacy = []
for word in snapshot[1]:
    if word >= 1 << 31:
        word -= 1 << 32
    legacy.append(word)
restored.setstate((2, tuple(legacy), marker))
assert restored.getstate() == snapshot
restored.seed(17)
assert restored.getstate()[2] is None

restored.setstate(snapshot)
before = restored.getstate()
words = snapshot[1]
raises(ValueError, restored.setstate, (1, words, None))
raises(ValueError, restored.setstate, (3, words[:-1] + (625,), None))
raises(ValueError, restored.setstate, (2, words[:-1] + (-1,), None))
raises(ValueError, restored.setstate, (3, (0,), None))
raises(OverflowError, restored.setstate, (3, (1 << 32,) + words[1:], None))
raises(OverflowError, restored.setstate, (3, (-1,) + words[1:], None))
raises(TypeError, restored.setstate, (3, ("word",) + words[1:], None))
raises(TypeError, restored.setstate, (3, list(words), None))
raises(TypeError, restored.seed, "seed")
raises(ValueError, restored.getrandbits, -(1 << 100))
raises(OverflowError, restored.getrandbits, 1 << 64)
raises(TypeError, restored.getrandbits, 2.0)
raises(TypeError, restored.getrandbits, k=2)
raises(TypeError, restored.randrange, 7, None, True)
raises(TypeError, restored.randrange, 0, 0, 0.0)
raises(ValueError, restored.randrange, 0, 0, 0)
raises(ValueError, restored.randrange, 0, 4, -1)
assert restored.getstate() == before


class Draws(Random):
    def seed(self, a=None, version=2):
        self.initial_seed = a
        self.widths = []
        Random.seed(self, a, version)

    def getrandbits(self, k, /):
        self.widths.append(k)
        if len(self.widths) == 1:
            Random.seed(self, 1)
            return 7
        return Random.getrandbits(self, k)


override = Draws(3)
assert override.initial_seed == 3
expected = Random(1)
assert override.randrange(5) == expected.randrange(5)
assert override.widths == [3, 3]
assert override.getstate() == expected.getstate()


class EntropyReader:
    def __init__(self, path, mode):
        assert path == "/dev/urandom"
        assert mode == "rb"
        self.reads = 0

    def __enter__(self):
        return self

    def __exit__(self, kind, error, traceback):
        closed.append(True)

    def read(self, size):
        self.reads += 1
        if self.reads == 1:
            assert size == 2496
            return b"\x01\x00\x00\x00"
        assert size == 2492
        if fail:
            return b""
        return bytes(2492)


closed = []
original = random._FileIO
try:
    random._FileIO = EntropyReader
    fail = False
    automatic = Random()
    # The entropy key is [1, 0, ...], with all trailing zero words included.
    assert automatic.getrandbits(32) == 2109933210
    assert closed == [True]
    fail = True
    before = automatic.getstate()
    raises(OSError, automatic.seed, None)
    assert automatic.getstate() == before
    assert closed == [True, True]
finally:
    random._FileIO = original
