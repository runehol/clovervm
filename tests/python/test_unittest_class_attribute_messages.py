# Python test set -- CloverVM unittest minimal-message and reentry contract
#
# API behavior adapted from CPython's Lib/unittest/case.py at v3.14.10.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# Message identity and omitted diagnostic callbacks are CloverVM-specific.

import unittest


class Parent:
    present = None


class Child(Parent):
    pass


class Case(unittest.TestCase):
    def test_empty(self):
        pass


class Failure(Exception):
    pass


class CustomCase(Case):
    failureException = Failure


case = Case('test_empty')
custom = CustomCase('test_empty')
obj = Child()


class Message:
    def __bool__(self):
        raise ValueError('message truth tested')

    def __str__(self):
        raise ValueError('message formatted')

    def __repr__(self):
        raise ValueError('message repr')


class NoRepr:
    def __repr__(self):
        raise ValueError('operand repr')


checks = [
    ('assertIsSubclass', (Child, Parent), (Parent, Child)),
    ('assertNotIsSubclass', (Parent, Child), (Child, (Parent, 42))),
    ('assertHasAttr', (obj, 'present'), (obj, 'missing')),
    ('assertNotHasAttr', (obj, 'missing'), (obj, 'present')),
    ('assertIsSubclass', (Child, Parent), (NoRepr(), Parent)),
    ('assertNotIsSubclass', (Parent, Child), (NoRepr(), Parent)),
    ('assertHasAttr', (obj, 'present'), (NoRepr(), 'missing')),
]

for entry in checks:
    name = entry[0]
    good = entry[1]
    bad = entry[2]
    method = getattr(unittest.TestCase, name)
    # A passing assertion never touches the receiver's failure path.
    assert method(None, *good) is None
    for receiver in (case, custom):
        for msg in (None, '', False, 'message', [1], Message()):
            assert method(receiver, *good, msg=msg) is None
            try:
                method(receiver, *bad, msg=msg)
            except receiver.failureException as error:
                assert error.args == (msg,)
                assert error.args[0] is msg
            else:
                assert False
        for args in (bad, bad + (None,), bad + ('positional',)):
            try:
                method(receiver, *args)
            except receiver.failureException as error:
                if len(args) == 2:
                    assert error.args == (None,)
                else:
                    assert error.args[0] is args[2]
            else:
                assert False


# Keep the existing issubclass primitive's nonclass/empty-tuple limitation
# explicit. CPython's negative assertion instead succeeds for this case.
for name in ('assertIsSubclass', 'assertNotIsSubclass'):
    method = getattr(unittest.TestCase, name)
    try:
        method(custom, 1, (), msg='nonclass')
    except Failure as error:
        assert error.args == ('nonclass',)
    else:
        assert False


# Invalid superclass/name errors propagate without invoking fail or replacing
# their messages. A nonclass lhs with an invalid superclass is a failure.
events = []


class ReentrantCase(Case):
    def fail(self, msg=None):
        events.append(msg)
        assert self.assertIsSubclass(Child, Parent) is None
        assert self.assertNotIsSubclass(Parent, Child) is None
        assert self.assertHasAttr(obj, 'present') is None
        assert self.assertNotHasAttr(obj, 'missing') is None
        # Allocate while operands and msg remain live across reentry.
        scratch = []
        for i in range(25):
            scratch.append([i])
        assert scratch[-1] == [24]
        raise Failure(msg)


reentrant = ReentrantCase('test_empty')
for entry in checks:
    name = entry[0]
    bad = entry[2]
    method = getattr(unittest.TestCase, name)
    msg = [name]
    try:
        method(reentrant, *bad, msg=msg)
    except Failure as error:
        assert error.args[0] is msg
        assert events[-1] is msg
    else:
        assert False
assert len(events) == len(checks)
before = len(events)
for entry in (
    ('assertIsSubclass', (Child, 42)),
    ('assertNotIsSubclass', (Child, (42, Parent))),
    ('assertHasAttr', (obj, 42)),
    ('assertNotHasAttr', (obj, None)),
):
    name = entry[0]
    args = entry[1]
    method = getattr(unittest.TestCase, name)
    try:
        method(reentrant, *args, msg=Message())
    except TypeError:
        pass
    else:
        assert False
assert len(events) == before


# A TypeError from fail itself must escape. For ordinary subclass failures,
# fail is outside the try that handles issubclass errors.
class TypeErrorCase(Case):
    def fail(self, msg=None):
        events.append(msg)
        raise TypeError(msg)


type_error_case = TypeErrorCase('test_empty')
for entry in checks:
    name = entry[0]
    bad = entry[2]
    before = len(events)
    method = getattr(unittest.TestCase, name)
    try:
        method(type_error_case, *bad, msg=name)
    except TypeError as error:
        assert error.args == (name,)
    else:
        assert False
    assert events[before:] == [name]


# TestCase.run uses the configured failure class to record the new assertions.
class RecordedCase(CustomCase):
    def test_empty(self):
        self.assertHasAttr(obj, 'missing', msg='recorded')


recorded = RecordedCase('test_empty')
result = recorded.run()
assert result.testsRun == 1
assert result.errors == []
assert result.successes == []
assert result.failures[0][0] is recorded
assert result.failures[0][1].args == ('recorded',)
