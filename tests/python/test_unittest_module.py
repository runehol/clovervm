# Python test set -- unittest module
#
# Lifecycle scenarios adapted from CPython's
# Lib/unittest/case.py at v3.14.5 and local CPython lifecycle probes.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Local adaptations use explicit run() and inspect raw exception entries.

import unittest


class Assertions(unittest.TestCase):
    def test_assertions(self):
        obj = []
        self.assertTrue([1])
        self.assertFalse([])
        self.assertEqual([1], [1])
        self.assertNotEqual(1, 2)
        self.assertIs(obj, obj)
        self.assertIsNot(obj, [])
        self.assertIsNone(None)
        self.assertIsNotNone(obj)
        self.assertIn(1, [1, 2])
        self.assertNotIn(3, [1, 2])
        self.assertIsInstance(self, unittest.TestCase)
        self.assertIsInstance(1, (int, str))


case = Assertions('test_assertions')
r = case.run()
assert r.testsRun == 1
assert r.successes == [case]
assert r.wasSuccessful()

# Each assertion has a failing path, and preserves an explicit message.
checks = [
    (case.assertTrue, (False,)),
    (case.assertFalse, (True,)),
    (case.assertEqual, (1, 2)),
    (case.assertNotEqual, (1, 1)),
    (case.assertIs, ([], [])),
    (case.assertIsNot, (None, None)),
    (case.assertIsNone, (1,)),
    (case.assertIsNotNone, (None,)),
    (case.assertIn, (3, [1])),
    (case.assertNotIn, (1, [1])),
    (case.assertIsInstance, (1, str)),
]
for entry in checks:
    check = entry[0]
    args = entry[1]
    try:
        check(case, *args, msg='failure message')
        assert False
    except AssertionError as error:
        assert error.args == ('failure message',)


def raises(value, *, msg):
    assert value == 7
    assert msg == 'forwarded'
    raise ValueError('expected')


def returns():
    return 42


assert case.assertRaises(ValueError, raises, 7, msg='forwarded') is None
case.assertRaises((TypeError, ValueError), raises, 7, msg='forwarded')
caught = False
try:
    case.assertRaises(ValueError, returns)
except AssertionError:
    caught = True
assert caught
try:
    case.assertRaises(TypeError, raises, 7, msg='forwarded')
    assert False
except ValueError as error:
    assert error.args == ('expected',)
try:
    case.assertRaises(int, returns)
    assert False
except TypeError:
    pass


class Lifecycle(unittest.TestCase):
    def setUp(self):
        self.events.append('setup')
        if self.phase == 'setup':
            raise self.error('setup')

    def test_named(self):
        self.events.append('test')
        if self.phase == 'test' or self.phase == 'both':
            raise self.error('test')

    def tearDown(self):
        self.events.append('teardown')
        if self.phase == 'teardown' or self.phase == 'both':
            raise ValueError('teardown')


def lifecycle(phase, error):
    test = Lifecycle('test_named')
    test.phase = phase
    test.error = error
    test.events = []
    return test


for phase in ('setup', 'test', 'teardown', 'both', 'ok'):
    for error_type in (AssertionError, ValueError):
        test = lifecycle(phase, error_type)
        result = unittest.TestResult()
        assert test.run(result) is result
        assert result.testsRun == 1
        if phase == 'setup':
            assert test.events == ['setup']
        else:
            assert test.events == ['setup', 'test', 'teardown']
        if phase == 'ok':
            assert result.successes == [test]
            assert result.wasSuccessful()
        else:
            assert result.successes == []
            assert not result.wasSuccessful()
            failures = 0
            errors = 0
            if phase != 'teardown':
                if error_type is AssertionError:
                    failures = 1
                else:
                    errors = 1
            if phase == 'teardown' or phase == 'both':
                errors += 1
            assert len(result.failures) == failures
            assert len(result.errors) == errors
            for entry in result.failures:
                recorded_test = entry[0]
                exception = entry[1]
                assert recorded_test is test
                assert isinstance(exception, AssertionError)
            for entry in result.errors:
                recorded_test = entry[0]
                exception = entry[1]
                assert recorded_test is test
                assert isinstance(exception, ValueError)


class TeardownFailure(Assertions):
    def tearDown(self):
        raise AssertionError('teardown assertion')


result = TeardownFailure('test_assertions').run()
assert len(result.failures) == 1
assert result.errors == []
assert result.successes == []


class CustomFailure(Exception):
    pass


class CustomCase(unittest.TestCase):
    failureException = CustomFailure

    def test_custom(self):
        self.fail('custom')


result = CustomCase('test_custom').run()
assert isinstance(result.failures[0][1], CustomFailure)

suite = unittest.TestSuite([case])
suite.addTest(unittest.TestSuite([lifecycle('test', ValueError)]))
suite.addTests([lifecycle('setup', AssertionError), case])
result = unittest.TestResult()
assert suite.run(result) is result
assert result.testsRun == 4
assert result.successes == [case, case]
assert len(result.failures) == 1
assert len(result.errors) == 1
case.run(result)
assert result.testsRun == 5
assert len(result.successes) == 3

# KeyboardInterrupt propagates, while stopTest still runs.
class TrackingResult(unittest.TestResult):
    def stopTest(self, test):
        self.stopped = test


for phase in ('setup', 'test'):
    test = lifecycle(phase, KeyboardInterrupt)
    result = TrackingResult()
    caught = False
    try:
        test.run(result)
    except KeyboardInterrupt:
        caught = True
    assert caught
    assert result.stopped is test
    assert result.successes == []
    assert result.failures == []
    assert result.errors == []

class InterruptedTeardown(Assertions):
    def tearDown(self):
        raise KeyboardInterrupt()


test = InterruptedTeardown('test_assertions')
result = TrackingResult()
caught = False
try:
    test.run(result)
except KeyboardInterrupt:
    caught = True
assert caught
assert result.stopped is test
assert result.successes == []

try:
    Assertions('missing')
    assert False
except ValueError:
    pass
