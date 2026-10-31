# Independently written callback-order regressions checked against CPython 3.14.10.
import base64
import binascii


class MutatingLength(bytes):
    def __len__(self):
        assert base64.b64decode(base64.b64encode(bytearray(b'nested'))) == b'nested'
        self.calls += 1
        self.data[:] = self.replacement
        return self.reported

    def __bytes__(self):
        raise AssertionError('must use stored option payload')

    def __getitem__(self, key):
        raise AssertionError('must use stored option payload')


class MutatingFlag:
    def __bool__(self):
        assert base64.b32decode(base64.b32encode(bytearray(b'nested'))) == b'nested'
        self.calls += 1
        self.data[:] = self.replacement
        return self.result


def option(raw, data, replacement, reported):
    opt = MutatingLength(raw)
    opt.data = data
    opt.replacement = replacement
    opt.reported = reported
    opt.calls = 0
    return opt


def flag(data, replacement, result):
    opt = MutatingFlag()
    opt.data = data
    opt.replacement = replacement
    opt.result = result
    opt.calls = 0
    return opt


# Encoding consumes data before altchars' Python-visible length assertion.
s = bytearray(b'f')
a = option(b'-_', s, b'g', 2)
assert base64.b64encode(s, a) == b'Zg=='
assert bytes(s) == b'g'
assert a.calls == 1

# Decoding retains the bytearray through the length assertion and snapshots
# it for translation. A later validate callback cannot alter that snapshot.
s = bytearray(b'Zg==')
a = option(b'-_', s, b'Zw==', 2)
v = flag(s, b'ZA==', True)
assert base64.b64decode(s, a, v) == b'g'
assert bytes(s) == b'ZA=='
assert a.calls == 1
assert v.calls == 1

# Without translation, validate can mutate the bytes that will be decoded.
# Use equal-size mutation: CPython's native export disallows resizing here.
for result in (False, True):
    s = bytearray(b'Zg==')
    v = flag(s, b'Zw==', result)
    assert base64.b64decode(s, validate=v) == b'g'
    assert v.calls == 1

for case in ((base64.b16decode, b'66', b'67'),
             (base64.b32decode, b'MY======', b'M4======'),
             (base64.b32hexdecode, b'CO======', b'CS======')):
    decode = case[0]
    raw = case[1]
    changed = case[2]
    for result in (False, True):
        s = bytearray(raw)
        v = flag(s, changed, result)
        assert decode(s, casefold=v) == b'g'
        assert v.calls == 1

# Base32's initial padding check precedes map01 and casefold callbacks.
s = bytearray(b'x')
a = option(b'L', s, b'MY======', 1)
v = flag(s, b'M4======', True)
caught = False
try:
    base64.b32decode(s, v, a)
except binascii.Error:
    caught = True
assert caught
assert a.calls == 0
assert v.calls == 0

s = bytearray(b'MY======')
a = option(b'L', s, b'M4======', 1)
v = flag(s, b'MQ======', True)
assert base64.b32decode(s, v, a) == b'g'
assert bytes(s) == b'MQ======'
assert a.calls == 1
assert v.calls == 1

# Truth tests can change mutable options, but translation has already used them.
s = bytearray(b'-_8=')
a = bytearray(b'-_')
v = flag(a, b'??', True)
assert base64.b64decode(s, a, v) == b'\xfb\xff'
a = bytearray(b'L')
v = flag(a, b'X', True)
assert base64.b32decode(bytearray(b'01======'), v, a) == b'r'

# Resizing via Base16/Base32 flag hooks is allowed and changes consumed data.
s = bytearray(b'66')
v = flag(s, b'666F6F', False)
assert base64.b16decode(s, v) == b'foo'
for case in ((base64.b32decode, b'MY======', b'MZXW6==='),
                 (base64.b32hexdecode, b'CO======', b'CPNMU===')):
    decode = case[0]
    s = bytearray(case[1])
    v = flag(s, case[2], False)
    assert decode(s, v) == b'foo'

# Even an all-alphabet replacement must be checked for padding after callbacks.
for replacement in (b'M', b'MZXW6'):
    s = bytearray(b'MY======')
    v = flag(s, replacement, False)
    caught = False
    try:
        base64.b32decode(s, v)
    except binascii.Error as error:
        caught = True
        assert str(error) == 'Incorrect padding'
    assert caught

# A false truth result still observes mutation; invalid alphabet wins over
# the final length check, just as in the CPython wrapper.
s = bytearray(b'MY======')
v = flag(s, b'!', False)
caught = False
try:
    base64.b32decode(s, v)
except binascii.Error as error:
    caught = True
    assert str(error) == 'Non-base32 digit found'
assert caught


class FailingFlag:
    def __bool__(self):
        raise RuntimeError('flag failure')


class FailingLength(bytes):
    def __len__(self):
        raise RuntimeError('length failure')


# User exceptions propagate unchanged, before any alphabet decoding.
for case in ((base64.b16decode, bytearray(b'!'), FailingFlag()),
             (base64.b32decode, bytearray(b'!!!!!!!!'), FailingFlag()),
             (base64.b32hexdecode, bytearray(b'!!!!!!!!'), FailingFlag())):
    caught = False
    try:
        case[0](case[1], case[2])
    except RuntimeError as error:
        caught = True
        assert str(error) == 'flag failure'
    assert caught
caught = False
try:
    base64.b64decode(bytearray(b'!'), validate=FailingFlag())
except RuntimeError as error:
    caught = True
    assert str(error) == 'flag failure'
assert caught
for call in (base64.b64encode, base64.b64decode):
    caught = False
    try:
        call(bytearray(b'!!!!'), FailingLength(b'-_'))
    except RuntimeError as error:
        caught = True
        assert str(error) == 'length failure'
    assert caught
caught = False
try:
    base64.b32decode(bytearray(b'!!!!!!!!'), map01=FailingLength(b'L'))
except RuntimeError as error:
    caught = True
    assert str(error) == 'length failure'
assert caught
