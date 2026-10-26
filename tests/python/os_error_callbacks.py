# Original constructor regressions checked against CPython 3.14.5.
import errno

hash_calls = 0
hash_failure = ValueError('errno hash failed')
class HashRaises(int):
    def __hash__(self):
        global hash_calls
        hash_calls += 1
        raise hash_failure

for payload in (errno.ENOENT, 1 << 100):
    try:
        OSError(HashRaises(payload), 'missing')
        assert False
    except ValueError as caught:
        assert caught is hash_failure
assert hash_calls == 2

class HashMiss(int):
    def __hash__(self):
        return 0
    def __eq__(self, other):
        raise ValueError('different hashes must not compare')

number = HashMiss(errno.ENOENT)
e = OSError(number, 'missing')
assert e.__class__ is OSError
assert e.errno is number

comparison_calls = 0
truth_calls = 0
class Decision:
    def __bool__(self):
        global truth_calls
        truth_calls += 1
        return True

class Redirect(int):
    def __hash__(self):
        return errno.EACCES
    def __eq__(self, other):
        global comparison_calls
        comparison_calls += 1
        assert other == errno.EACCES
        return Decision()

number = Redirect(errno.ENOENT)
e = OSError(number, 'redirected', 'path')
assert e.__class__ is PermissionError
assert e.errno is number and e.filename == 'path'
assert comparison_calls == 1 and truth_calls == 1

comparison_failure = ValueError('errno comparison failed')
class CompareRaises(int):
    def __hash__(self):
        return errno.ENOENT
    def __eq__(self, other):
        raise comparison_failure

try:
    OSError(CompareRaises(errno.ENOENT), 'missing')
    assert False
except ValueError as caught:
    assert caught is comparison_failure

class Unhashable(int):
    __hash__ = None

try:
    OSError(Unhashable(errno.ENOENT), 'missing')
    assert False
except TypeError:
    pass

reentry_calls = 0
class Reentrant(int):
    def __hash__(self):
        global reentry_calls
        reentry_calls += 1
        nested = OSError(errno.EEXIST, 'nested')
        assert nested.__class__ is FileExistsError
        try:
            OSError(HashRaises(1), 'nested failure')
            assert False
        except ValueError as caught:
            assert caught is hash_failure
        return int.__hash__(self)

for i in range(30):
    number = Reentrant(errno.ENOENT)
    e = OSError(number, ['message', i], ['path', i])
    assert e.__class__ is FileNotFoundError
    assert e.errno is number
    assert e.strerror == ['message', i]
    assert e.filename == ['path', i]
assert reentry_calls == 30

# A failed callback must not poison the next ordinary constructor call.
assert OSError(errno.ENOENT, 'recovered').__class__ is FileNotFoundError

# Warm hash/equality dispatch, then mutate the errno subclass methods.
class Switching(int):
    def __hash__(self):
        return 0

number = Switching(errno.ENOENT)
for i in range(30):
    assert OSError(number, 'miss').__class__ is OSError

def select_hash(self):
    return errno.ENOENT
Switching.__hash__ = select_hash
for i in range(30):
    assert OSError(number, 'hit').__class__ is FileNotFoundError

def reject_equality(self, other):
    return False
Switching.__eq__ = reject_equality
for i in range(30):
    assert OSError(number, 'miss').__class__ is OSError

del Switching.__eq__
for i in range(30):
    assert OSError(number, 'hit').__class__ is FileNotFoundError
