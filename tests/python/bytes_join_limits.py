# Explicit CloverVM compatibility limits; not a CPython differential test.
class Hinted:
    def __init__(self):
        self.done = False
    def __iter__(self):
        return self
    def __length_hint__(self):
        raise AssertionError('length hints remain outside this slice')
    def __next__(self):
        if self.done:
            raise StopIteration
        self.done = True
        return b'payload'

assert b'-'.join(Hinted()) == b'payload'

class SequenceOnly:
    def __getitem__(self, index):
        if index == 0:
            return b'payload'
        raise IndexError

try:
    b'-'.join(SequenceOnly())
    assert False
except TypeError:
    pass
assert not hasattr(bytearray, 'join')
