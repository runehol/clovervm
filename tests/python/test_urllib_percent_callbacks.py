from urllib import parse

# Safe normalization occurs after the initial empty shortcut and before payload
# consumption. Reentry has no shared quoter/decode cache to corrupt.
payload = bytearray(b'a b')
calls = []
class Safe:
    def __iter__(self):
        calls.append('iter')
        payload[0] = 99
        assert parse.quote_from_bytes(b'%', b'') == '%25'
        return iter([32])
assert parse.quote_from_bytes(payload, Safe()) == 'c b'
assert calls == ['iter']
assert parse.quote_from_bytes(b'', Safe()) == ''
assert calls == ['iter']

class Shrink:
    def __iter__(self):
        payload.clear()
        return iter([])
assert parse.quote_from_bytes(payload, Shrink()) == ''

class Failure(Exception):
    pass
class BadSafe:
    def __iter__(self):
        raise Failure('safe')
for i in range(20):
    try:
        parse.quote_from_bytes(b'a', BadSafe())
        assert False
    except Failure:
        pass
    assert parse.quote_from_bytes(b'%') == '%25'

# quote_plus retains the no-space shortcut and safe + space operation, including
# the bytearray path which always performs that addition.
assert parse.quote_plus(b'a', [47]) == 'a'
for raw in (b'a b', bytearray(b'a')):
    try:
        parse.quote_plus(raw, [47])
        assert False
    except TypeError:
        pass

payload = bytearray(b'a b')
class AddingSafe:
    def __add__(self, space):
        assert space == b' '
        payload[0] = 99
        assert parse.quote_plus('x y') == 'x+y'
        return b' '
assert parse.quote_plus(payload, AddingSafe()) == 'c+b'

# Warmed iterator lookup must observe replacing the class method.
class ChangingSafe:
    def __iter__(self):
        return iter([32])
safe = ChangingSafe()
for i in range(12):
    assert parse.quote_from_bytes(b'a b', safe) == 'a b'
def no_safe(self):
    return iter([])
ChangingSafe.__iter__ = no_safe
for i in range(12):
    assert parse.quote_from_bytes(b'a b', safe) == 'a%20b'
