# Independently written callback ordering, checked against CPython 3.14.10.
import base64


class Flag:
    def __init__(self, data, result):
        self.data = data
        self.result = result
        self.calls = 0

    def __bool__(self):
        self.calls += 1
        # Reenter both public functions, allocate, and resize the original data.
        assert base64.b85decode(base64.b85encode(b'nested')) == b'nested'
        garbage = []
        for i in range(24):
            garbage.append([i])
        self.data[:] = b'changed'
        if self.result is None:
            raise ValueError('pad failure')
        return self.result


for n in range(9):
    values = []
    for i in range(n):
        values.append(i)
    raw = bytes(values)
    for result in (False, True, None):
        data = bytearray(raw)
        flag = Flag(data, result)
        expected = base64.b85encode(raw, result)
        if n % 4 and result is None:
            try:
                base64.b85encode(data, flag)
            except ValueError as error:
                assert str(error) == 'pad failure'
            else:
                raise AssertionError('missing callback error')
        else:
            assert base64.b85encode(data, flag) == expected
        assert flag.calls == (1 if n % 4 else 0)
        assert bytes(data) == (b'changed' if n % 4 else raw)
# Input validation precedes any pad callback.
flag = Flag(bytearray(), False)
try:
    base64.b85encode(None, flag)
except TypeError:
    pass
else:
    raise AssertionError('missing input error')
assert flag.calls == 0
# Changing the callback implementation must be observed by warmed calls.
class MutableFlag:
    def __bool__(self):
        return False
flag = MutableFlag()
for i in range(3):
    assert base64.b85encode(b'f', flag) == b'W&'
def true(self):
    return True
MutableFlag.__bool__ = true
for i in range(3):
    assert base64.b85encode(b'f', flag) == b'W&i*H'


class LengthFlag:
    def __init__(self):
        self.calls = 0
    def __len__(self):
        self.calls += 1
        return 1
flag = LengthFlag()
assert base64.b85encode(b'f', flag) == b'W&i*H'
assert flag.calls == 1
assert base64.b85encode(b'foob', flag) == b'W^Zp|'
assert flag.calls == 1
