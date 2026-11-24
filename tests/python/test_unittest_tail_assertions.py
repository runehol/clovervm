# Scenarios adapted from CPython's Lib/test/test_unittest/test_case.py at
# v3.14.10. Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Local cases cover minimal messages and the existing builtin matching limits.

import unittest


class Failure(Exception):
    pass


class Case(unittest.TestCase):
    failureException = Failure


def raised(expected, method, *args, **kwargs):
    try:
        method(*args, **kwargs)
    except expected as error:
        return error
    raise AssertionError('expected an exception')


def check_failure(method, operands, message):
    error = raised(Failure, method, *operands, msg=message)
    assert len(error.args) == 1
    assert error.args[0] is message


events = []


class Truth:
    def __bool__(self):
        events.append('truth')
        if truth_error is not None:
            raise truth_error
        return matched


class Text(str):
    def startswith(self, candidate):
        return match('startswith', candidate)

    def endswith(self, candidate):
        return match('endswith', candidate)


def match(name, candidate):
    events.append((name, candidate))
    if call_error is not None:
        raise call_error
    if candidate is not original:
        raise RuntimeError('unexpected diagnostic retry')
    return Truth()


class Tails(tuple):
    def __iter__(self):
        events.append('iterate')
        return iter((b'cross-family',))


case = Case('setUp')
for entry in (
    ('StartsWith', 'prefix', 'startswith', 'a', b'a'),
    ('EndsWith', 'suffix', 'endswith', 'c', b'c'),
):
    direction = entry[0]
    keyword = entry[1]
    operation = entry[2]
    text_hit = entry[3]
    binary_hit = entry[4]
    positive = getattr(case, 'assert' + direction)
    negative = getattr(case, 'assertNot' + direction)
    message = []
    assert positive(s='abc', **{keyword: text_hit}) is None
    assert negative(s='abc', **{keyword: 'x', 'msg': message}) is None
    assert positive('abc', '', message) is None
    assert negative('abc', ()) is None
    check_failure(positive, ('abc', ()), message)
    assert raised(Failure, negative, 'abc', text_hit).args == (None,)
    for method in (positive, negative):
        raised(TypeError, method, 'abc')
        raised(TypeError, method, 'abc', text_hit, None, None)
        raised(TypeError, method, 'abc', **{'candidate': text_hit})
        raised(TypeError, method, 'abc', text_hit, **{keyword: text_hit})

    for operands in (('abc', text_hit, 'x'),
                     (b'abc', binary_hit, b'x'),
                     (bytearray(b'abc'), bytearray(binary_hit), b'x')):
        s = operands[0]
        hit = operands[1]
        miss = operands[2]
        assert positive(s, hit) is None
        assert negative(s, (miss,)) is None
        assert positive(s, (miss, hit, None)) is None
        check_failure(negative, (s, (miss, hit, None)), message)
        for method in (positive, negative):
            raised(TypeError, method, s, (miss, None, hit))
            raised(TypeError, method, s, ((hit,), hit))
            raised(TypeError, method, s, 1)

    for method in (positive, negative):
        check_failure(method, ([], text_hit), message)
        check_failure(method, ([], (bytearray(binary_hit),)), message)
        check_failure(method, (b'abc', text_hit), message)
        check_failure(method, ('abc', binary_hit), message)
        # The error-path type scan can see a mismatch after the invalid entry.
        check_failure(method, ('abc', (0, binary_hit)), message)
        raised(AttributeError, method, [], 1)
        raised(AttributeError, method, [], ())
        raised(TypeError, method, b'abc', bytearray(binary_hit))

    # The override receives the original tuple and its result is tested once.
    # Iterating this tuple would expose a different candidate to the override.
    original = Tails((text_hit,))
    receiver = Text('abc')
    call_error = None
    truth_error = None
    for outcome in ((positive, True, True), (positive, False, False),
                    (negative, False, True), (negative, True, False)):
        method = outcome[0]
        matched = outcome[1]
        passes = outcome[2]
        events.clear()
        if passes:
            assert method(receiver, original) is None
        else:
            check_failure(method, (receiver, original), message)
        assert events == [(operation, original), 'truth']
        assert events[0][1] is original

    for method in (positive, negative):
        for error_type in (AttributeError, TypeError, ValueError):
            exc = error_type('callback')
            original = (text_hit,)
            events.clear()
            call_error = exc
            assert raised(error_type, method, receiver, original) is exc
            assert events == [(operation, original)]

            call_error = None
            truth_error = exc
            events.clear()
            assert raised(error_type, method, receiver, original) is exc
            assert events == [(operation, original), 'truth']

        # The error-path tuple scan uses ordinary iteration, unlike the
        # diagnostic retry omitted after a successful match.
        original = Tails((text_hit,))
        call_error = AttributeError('callback')
        truth_error = None
        events.clear()
        check_failure(method, (receiver, original), message)
        assert events == [(operation, original), 'iterate']

        call_error = None
        truth_error = TypeError('truth')
        events.clear()
        check_failure(method, (receiver, original), message)
        assert events == [(operation, original), 'truth', 'iterate']

        truth_error = ValueError('truth')
        events.clear()
        assert raised(ValueError, method, receiver, original) is truth_error
        assert events == [(operation, original), 'truth']
