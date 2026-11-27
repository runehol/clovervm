"""Deterministic, non-cryptographic MT19937 generators.

Integer seeds and the supported draw methods reproduce CPython 3.14.10.
An omitted seed or None reads from the system entropy device.
"""

import _random
from _io import FileIO as _FileIO

__all__ = ("Random",)
_ONE = 1
_ENTROPY_BYTES = 624 * 4


def _read_seed():
    with _FileIO("/dev/urandom", "rb") as source:
        seed = b""
        while len(seed) < _ENTROPY_BYTES:
            chunk = source.read(_ENTROPY_BYTES - len(seed))
            if not chunk:
                raise OSError("could not read enough bytes from /dev/urandom")
            seed += chunk
    return seed


def _index(value):
    if not isinstance(value, int):
        raise TypeError("expected an integer")
    return int.__index__(value)


class Random:
    """Independent MT19937 generator seeded by an integer or OS entropy."""

    VERSION = 3

    def __init__(self, x=None):
        self.seed(x)

    def seed(self, a=None, version=2):
        """Replace the generator state. Integer seeds use every magnitude bit."""
        if a is None:
            engine = _random.seed_from_entropy(_read_seed())
        else:
            engine = _random.seed_from_int(a)
        self._state = (engine, None)

    def random(self, /):
        """Return a random float in the half-open interval [0, 1)."""
        return _random.random(self._state[0])

    def getrandbits(self, k, /):
        """Return a nonnegative integer with k random bits."""
        return _random.getrandbits(self._state[0], k)

    def _randbelow(self, n):
        width = n.bit_length()
        result = self.getrandbits(width)
        while result >= n:
            result = self.getrandbits(width)
        return result

    def randrange(self, start, stop=None, step=_ONE):
        """Choose one element of the given integer range."""
        first = _index(start)
        if stop is None:
            if step is not _ONE:
                raise TypeError("Missing a non-None stop argument")
            if first <= 0:
                raise ValueError("empty range for randrange()")
            return self._randbelow(first)

        width = _index(stop) - first
        stride = _index(step)
        if stride == 1:
            if width <= 0:
                raise ValueError("empty range for randrange()")
            return first + self._randbelow(width)
        if stride > 0:
            count = (width + stride - 1) // stride
        elif stride < 0:
            count = (width + stride + 1) // stride
        else:
            raise ValueError("zero step for randrange()")
        if count <= 0:
            raise ValueError("empty range for randrange()")
        return first + stride * self._randbelow(count)

    def getstate(self):
        """Return a portable snapshot of the generator."""
        state = self._state
        return (3, _random.getstate(state[0]), state[1])

    def setstate(self, state):
        """Restore a version-3 or legacy version-2 snapshot."""
        if not isinstance(state, tuple):
            raise TypeError("random state must be a tuple")
        if tuple.__len__(state) != 3:
            raise ValueError("random state must have three entries")
        version = _index(tuple.__getitem__(state, 0))
        if version != 3 and version != 2:
            raise ValueError("unsupported random state version")
        engine = _random.restore_state(tuple.__getitem__(state, 1), version == 2)
        self._state = (engine, tuple.__getitem__(state, 2))
