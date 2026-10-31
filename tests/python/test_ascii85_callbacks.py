# Independently written callback/snapshot assertions, CPython 3.14.10 reference.
import base64


class Flag:
    def __init__(self, name, events, result, data=None, replacement=None):
        self.name = name
        self.events = events
        self.result = result
        self.data = data
        self.replacement = replacement

    def __bool__(self):
        self.events.append(self.name)
        assert base64.a85decode(base64.a85encode(b'nested')) == b'nested'
        garbage = []
        for i in range(24):
            garbage.append([i])
        if self.data is not None:
            self.data[:] = self.replacement
        if self.result is None:
            raise ValueError(self.name)
        return self.result


for case in ((b'', []), (b'\0', ['pad']),
                             (b'\0\0\0\0', []), (b'    ', ['fold']),
                             (b'abcde', ['fold', 'fold', 'pad'])):
    raw = case[0]
    expected_events = case[1]
    events = []
    data = bytearray(raw)
    fold = Flag('fold', events, True, data, b'changed')
    pad = Flag('pad', events, True, data, b'changed')
    adobe = Flag('adobe', events, True, data, b'changed')
    expected = base64.a85encode(raw, foldspaces=True, pad=True, adobe=True, wrapcol=5)
    assert base64.a85encode(data, foldspaces=fold, pad=pad, adobe=adobe, wrapcol=5) == expected
    assert events == expected_events + ['adobe', 'adobe', 'adobe', 'adobe']
# The word snapshot survives even a resizing callback in the first group.
events = []
data = bytearray(b'     ')
assert base64.a85encode(data, foldspaces=Flag('fold', events, False, data, b'')) == b'+<VdL+9'
assert events == ['fold', 'fold']
for truth in (False, True):
    events = []
    data = bytearray(b'<~y~>' if truth else b'y')
    replacement = b'<~!!!!!~>' if truth else b'!!!!!'
    adobe = Flag('adobe', events, truth, data, replacement)
    assert base64.a85decode(data, adobe=adobe) == b'\0\0\0\0'
    assert events == ['adobe']
# A fold callback runs for whitespace too, but scanning has already frozen data.
events = []
data = bytearray(b' y')
fold = Flag('fold', events, True, data, b'z')
assert base64.a85decode(data, foldspaces=fold) == b'    '
assert events == ['fold', 'fold']


class Ignored:
    def __init__(self, data):
        self.data = data
        self.seen = []
    def __contains__(self, ch):
        self.seen.append(ch)
        self.data[:] = b'z'
        assert base64.a85decode(b'z') == b'\0\0\0\0'
        return ch == 32


data = bytearray(b' !!!!!')
ignored = Ignored(data)
assert base64.a85decode(data, ignorechars=ignored) == b'\0\0\0\0'
assert ignored.seen == [32]
# ignorechars stays live across fold callbacks.
ignore = bytearray()
class ChangeIgnore:
    def __bool__(self):
        ignore[:] = b' '
        return False
assert base64.a85decode(b' ', foldspaces=ChangeIgnore(), ignorechars=ignore) == b''
# Exception ordering: invalid input precedes flags; fold precedes pad and adobe.
events = []
for call in (base64.a85encode, base64.a85decode):
    try:
        call(None, adobe=Flag('adobe', events, None))
    except TypeError:
        pass
    else:
        raise AssertionError('input validation order')
assert events == []
try:
    base64.a85encode(b'a', foldspaces=Flag('fold', events, None),
                     pad=Flag('pad', events, None), adobe=Flag('adobe', events, None))
except ValueError as error:
    assert str(error) == 'fold'
else:
    raise AssertionError('missing flag failure')
assert events == ['fold']
# Warmed truth and membership caches must observe method replacement.
class MutableFlag:
    def __bool__(self):
        return False
flag = MutableFlag()
for i in range(3):
    assert base64.a85encode(b'    ', foldspaces=flag) == b'+<VdL'
def true(self):
    return True
MutableFlag.__bool__ = true
for i in range(3):
    assert base64.a85encode(b'    ', foldspaces=flag) == b'y'
class MutableIgnore:
    def __contains__(self, ch):
        return True
ignored = MutableIgnore()
for i in range(3):
    assert base64.a85decode(b' ', ignorechars=ignored) == b''
def fail(self, ch):
    raise ValueError('new contains')
MutableIgnore.__contains__ = fail
try:
    base64.a85decode(b' ', ignorechars=ignored)
except ValueError as error:
    assert str(error) == 'new contains'
else:
    raise AssertionError('stale contains')


class SequenceFlag:
    def __init__(self, values):
        self.values = values
        self.calls = 0
    def __bool__(self):
        result = self.values[self.calls]
        self.calls += 1
        return result


# Stateful options make separate reference evaluations observably necessary.
for case in (((True, False, False, False), b'<~A\noDS'),
             ((False, True, True, True), b'AoD\nS~>'),
             ((True, True, False, True), b'<~A\noDS~>')):
    adobe = SequenceFlag(case[0])
    assert base64.a85encode(b'foo', wrapcol=3, adobe=adobe) == case[1]
    assert adobe.calls == 4
# False decode folding still runs once for each non-digit/non-z character.
events = []
fold = Flag('fold', events, False)
assert base64.a85decode(b' \tz', foldspaces=fold) == b'\0\0\0\0'
assert events == ['fold', 'fold']
# __len__ truth fallback is handled by the existing truth protocol.
class LengthFlag:
    def __init__(self):
        self.calls = 0
    def __len__(self):
        self.calls += 1
        return 1
flag = LengthFlag()
assert base64.a85encode(b'    ', foldspaces=flag) == b'y'
assert flag.calls == 1
assert base64.a85decode(b'y', foldspaces=flag) == b'    '
assert flag.calls == 2


# Unlike input normalization, ignorechars retains ordinary subclass dispatch.
class IgnoredBytes(bytes):
    def __contains__(self, ch):
        assert ch == 32
        return True
assert base64.a85decode(b' ', ignorechars=IgnoredBytes(b'')) == b''


class SliceWidth(int):
    def __radd__(self, other):
        return None
# Preserve supported integer-subclass endpoint arithmetic and None slice bounds.
assert base64.a85encode(b'foob', wrapcol=SliceWidth(3)) == b'AoDTs\nTs'


class SelectedNegativeWidth(int):
    def __gt__(self, other):
        return True
# A comparison hook can select a negative step; its magnitude must stay bounded.
assert base64.a85encode(b'foo', wrapcol=SelectedNegativeWidth(-10 ** 100)) == b''
