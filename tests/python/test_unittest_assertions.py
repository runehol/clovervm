# Python test set -- unittest assertions
#
# Assertion behavior adapted from CPython's Lib/unittest/case.py at v3.14.5.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Local tests pin CloverVM's minimal msg contract instead of rich diagnostics.

import unittest


class Case(unittest.TestCase):
    def test_empty(self):
        pass


class Failure(Exception):
    pass


class CustomCase(Case):
    failureException = Failure


case = Case('test_empty')
custom = CustomCase('test_empty')
checks = [
    ('assertLess', (1, 2), (2, 1), {'a': 1, 'b': 2}),
    ('assertLessEqual', (1, 1), (2, 1), {'a': 1, 'b': 1}),
    ('assertGreater', (2, 1), (1, 2), {'a': 2, 'b': 1}),
    ('assertGreaterEqual', (1, 1), (1, 2), {'a': 1, 'b': 1}),
    ('assertNotIsInstance', (1, str), (1, int), {'obj': 1, 'cls': str}),
    ('assertListEqual', ([1], [1]), ([1], [2]), {'list1': [], 'list2': []}),
    ('assertTupleEqual', ((1,), (1,)), ((1,), (2,)), {'tuple1': (), 'tuple2': ()}),
    ('assertDictEqual', ({'a': 1}, {'a': 1}), ({'a': 1}, {'a': 2}), {'d1': {}, 'd2': {}}),
]

# Class attribute reads provide functions on both CPython and CloverVM.
for entry in checks:
    name = entry[0]
    good = entry[1]
    bad = entry[2]
    keywords = entry[3]
    method = getattr(unittest.TestCase, name)
    assert method(case, *good) is None
    assert method(case, **keywords) is None
    for receiver in (case, custom):
        for message in (None, '', False, 'failure', [1]):
            try:
                method(receiver, *bad, msg=message)
            except Exception as error:
                assert isinstance(error, receiver.failureException)
                assert len(error.args) == 1
                assert error.args[0] is message
            else:
                assert False
        try:
            method(receiver, *bad)
        except Exception as error:
            assert isinstance(error, receiver.failureException)
            assert error.args == (None,)
        else:
            assert False
        try:
            method(receiver, *bad, 'positional message')
        except Exception as error:
            assert isinstance(error, receiver.failureException)
            assert error.args == ('positional message',)
        else:
            assert False
    for entry in [
        ((), {}), ((case,), {}), ((case,) + good + (None, None), {}),
        ((case,) + good, {'unexpected': 1}),
        ((case,) + good, {'self': case}),
        ((case,) + good, keywords),
        ((case,) + good + (None,), {'msg': None}),
    ]:
        args = entry[0]
        kwargs = entry[1]
        try:
            method(*args, **kwargs)
        except TypeError:
            pass
        else:
            assert False

# Direct method calls continue to bind the receiver.
assert case.assertLess(1, 2) is None
try:
    unittest.TestCase.assertLess(None, 2, 1)
except AttributeError:
    pass
else:
    assert False
assert unittest.TestCase.assertLess(None, 1, 2) is None

for name in ('assertLess', 'assertLessEqual', 'assertGreater', 'assertGreaterEqual'):
    method = getattr(unittest.TestCase, name)
    try:
        method(case, 1, 'a')
    except TypeError:
        pass
    else:
        assert False
    try:
        method(case, float('nan'), 1)
    except AssertionError:
        pass
    else:
        assert False

# Comparisons return arbitrary objects, and assertions truth-test that result.
events = []
class Truth:
    def __bool__(self):
        events.append('bool')
        if truth_error:
            raise ValueError('truth')
        return truth_value


class Comparison:
    def __lt__(self, other):
        events.append('lt')
        return Truth()

    def __le__(self, other):
        events.append('le')
        return Truth()

    def __gt__(self, other):
        events.append('gt')
        return Truth()

    def __ge__(self, other):
        events.append('ge')
        return Truth()


for entry in [('assertLess', 'lt'), ('assertLessEqual', 'le'),
                        ('assertGreater', 'gt'), ('assertGreaterEqual', 'ge')]:
    name = entry[0]
    operation = entry[1]
    method = getattr(unittest.TestCase, name)
    for truth_value in (True, False):
        for truth_error in (True, False):
            events = []
            try:
                result = method(case, Comparison(), 0)
            except ValueError as error:
                assert truth_error and error.args == ('truth',)
            except AssertionError:
                assert not truth_value and not truth_error
            else:
                assert truth_value and not truth_error
                assert result is None
            assert events == [operation, 'bool']

class RaisesComparison:
    def __lt__(self, other):
        raise ValueError('comparison')


try:
    case.assertLess(RaisesComparison(), 1)
except ValueError as error:
    assert error.args == ('comparison',)
else:
    assert False

class Reflected:
    def __gt__(self, other):
        return True


assert case.assertLess(1, Reflected()) is None
assert case.assertNotIsInstance(1, (str, (list, tuple))) is None
for cls in (42, (str, 42)):
    try:
        case.assertNotIsInstance(1, cls)
    except TypeError:
        pass
    else:
        assert False
try:
    case.assertNotIsInstance(1, (int, 42))
except AssertionError:
    pass
else:
    assert False

class L(list):
    pass

class T(tuple):
    pass

class D(dict):
    pass


case.assertListEqual(L([1, [2]]), [1, [2]])
case.assertTupleEqual(T((1, [2])), (1, [2]))
case.assertDictEqual(D({'a': [1]}), {'a': [1]})
for entry in [
    ('assertListEqual', L([1]), [2]), ('assertListEqual', [], L([1])),
    ('assertTupleEqual', T((1,)), (2,)), ('assertTupleEqual', (), T((1,))),
    ('assertDictEqual', D({'a': 1}), {'a': 2}), ('assertDictEqual', {}, D({'a': 1})),
    ('assertListEqual', [1, 2], [2, 1]),
]:
    name = entry[0]
    left = entry[1]
    right = entry[2]
    try:
        getattr(unittest.TestCase, name)(case, left, right)
    except AssertionError:
        pass
    else:
        assert False

# Wrong types fail before length/comparison, even if equality would succeed.
class Wrong:
    def __eq__(self, other):
        raise ValueError('must not compare')

class Length(list):
    def __len__(self):
        events.append('len')
        raise length_error('length')

class TupleLength(tuple):
    def __len__(self):
        events.append('len')
        raise length_error('length')


for receiver in (case, custom):
    for entry in [
        ('assertListEqual', [], ((), {}, Wrong())),
        ('assertTupleEqual', (), ([], {}, Wrong())),
        ('assertDictEqual', {}, ([], (), Wrong())),
    ]:
        name = entry[0]
        good = entry[1]
        wrongs = entry[2]
        for wrong in wrongs:
            for pair in ((good, wrong), (wrong, good)):
                try:
                    getattr(unittest.TestCase, name)(receiver, *pair, msg='type')
                except Exception as error:
                    assert isinstance(error, receiver.failureException)
                    assert error.args == ('type',)
                else:
                    assert False

events = []
length_error = ValueError
try:
    case.assertListEqual(Length(), ())
except AssertionError:
    pass
else:
    assert False
assert events == []
for length_error in (TypeError, NotImplementedError, ValueError):
    for entry in [('assertListEqual', (Length(), [])),
                  ('assertListEqual', ([], Length())),
                  ('assertTupleEqual', (TupleLength(), ())),
                  ('assertTupleEqual', ((), TupleLength()))]:
        name = entry[0]
        pair = entry[1]
        events = []
        try:
            getattr(unittest.TestCase, name)(case, *pair, msg='length failure')
        except AssertionError as error:
            assert length_error is not ValueError
            assert error.args == ('length failure',)
        except ValueError as error:
            assert length_error is ValueError and error.args == ('length',)
        else:
            assert False
        assert events == ['len']

# Preserve overloaded container equality and its result's truth conversion.
class EqualList(list):
    def __len__(self):
        events.append('len')
        return 0

    def __eq__(self, other):
        events.append('eq')
        return Truth()

class EqualTuple(tuple):
    def __eq__(self, other):
        events.append('eq')
        return Truth()

class UnequalDict(dict):
    def __eq__(self, other):
        raise ValueError('must use ne')

    def __ne__(self, other):
        events.append('ne')
        return Truth()


truth_error = False
truth_value = True
events = []
case.assertListEqual(EqualList([1]), [2])
assert events == ['len', 'eq', 'bool']
events = []
case.assertTupleEqual(EqualTuple((1,)), (2,))
assert events == ['eq', 'bool']
truth_value = False
events = []
case.assertDictEqual(UnequalDict({'a': 1}), {'a': 2})
assert events == ['ne', 'bool']
truth_error = True
for entry in [('assertListEqual', (EqualList(), [])),
                   ('assertTupleEqual', (EqualTuple(), ())),
                   ('assertDictEqual', (UnequalDict(), {}))]:
    name = entry[0]
    pair = entry[1]
    try:
        getattr(unittest.TestCase, name)(case, *pair)
    except ValueError as error:
        assert error.args == ('truth',)
    else:
        assert False

# Comparison exceptions must not be mistaken for length failures.
class Element:
    def __eq__(self, other):
        raise TypeError('element equality')

for entry in [('assertListEqual', ([Element()], [Element()])),
                   ('assertTupleEqual', ((Element(),), (Element(),))),
                   ('assertDictEqual', ({'a': Element()}, {'a': Element()}))]:
    name = entry[0]
    pair = entry[1]
    try:
        getattr(unittest.TestCase, name)(case, *pair)
    except TypeError as error:
        assert error.args == ('element equality',)
    else:
        assert False

# assertEqual stays a direct comparison; no typed-container dispatch is added.
truth_error = False
truth_value = True
events = []
case.assertEqual(EqualList([1]), (2,))
assert events == ['eq', 'bool']

# Both lengths precede equality, including side effects from the right operand.
class OrderedLength(list):
    def __len__(self):
        events.append(self.label)
        return 0

    def __eq__(self, other):
        events.append('eq')
        return True


left = OrderedLength()
left.label = 'left'
right = OrderedLength()
right.label = 'right'
events = []
case.assertListEqual(left, right)
assert events == ['left', 'right', 'eq']
