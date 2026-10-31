# Python test set -- unittest subclass and attribute assertion outcomes
#
# Scenarios adapted from CPython's Lib/test/test_unittest/test_case.py and
# Lib/unittest/case.py at v3.14.10, with additional local boundary cases.
# Copyright (c) Python Software Foundation.
# The derived portions are licensed under the PSF License Agreement.
# This file also runs against CPython; rich failure messages are not compared.

import unittest


class Parent:
    present = None

    def method(self):
        return 1


class Child(Parent):
    pass


class Other:
    pass


class IntChild(int):
    pass


class TupleChild(tuple):
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


def outcome(method, receiver, args, keywords):
    try:
        result = method(receiver, *args, **keywords)
    except receiver.failureException:
        return 'failure'
    except TypeError:
        return 'TypeError'
    assert result is None
    return 'pass'


# The expected outcomes are explicit observations, not computed with the same
# predicate as the implementation. Invalid tuple entries matter only if visited.
subclasses = [
    (Child, Parent, 'pass', 'failure'),
    (Parent, Parent, 'pass', 'failure'),
    (Parent, Child, 'failure', 'pass'),
    (Child, Other, 'failure', 'pass'),
    (bool, int, 'pass', 'failure'),
    (IntChild, int, 'pass', 'failure'),
    (Child, object, 'pass', 'failure'),
    (Child, (), 'failure', 'pass'),
    (Child, (Other, Parent), 'pass', 'failure'),
    (Child, (Other, ((), Parent)), 'pass', 'failure'),
    (Child, (Parent, 42), 'pass', 'failure'),
    (Child, ((Other, Parent), 42), 'pass', 'failure'),
    (Child, (Other, ((),)), 'failure', 'pass'),
    (Child, TupleChild((Other, Parent)), 'pass', 'failure'),
    (Child, 42, 'TypeError', 'TypeError'),
    (Child, (42, Parent), 'TypeError', 'TypeError'),
    (Child, (Other, 42), 'TypeError', 'TypeError'),
    (Child, ((Other, 42), Parent), 'TypeError', 'TypeError'),
    (1, Parent, 'failure', 'failure'),
    (None, Parent, 'failure', 'failure'),
    (Child(), Parent, 'failure', 'failure'),
    (1, (Parent,), 'failure', 'failure'),
    (1, 42, 'failure', 'failure'),
]

count = 0
for receiver in (case, custom):
    for entry in subclasses:
        for assertion in (('assertIsSubclass', entry[2]),
                          ('assertNotIsSubclass', entry[3])):
            name = assertion[0]
            expected = assertion[1]
            method = getattr(unittest.TestCase, name)
            assert outcome(method, receiver, (entry[0], entry[1]), {}) == expected
            assert outcome(method, receiver, (),
                           {'cls': entry[0], 'superclass': entry[1],
                            'msg': 'message'}) == expected
            count += 2


instance = Child()
instance.own = False
attributes = [
    (instance, 'own', 'pass', 'failure'),
    (instance, 'present', 'pass', 'failure'),
    (instance, 'method', 'pass', 'failure'),
    (instance, 'absent', 'failure', 'pass'),
    (Child, 'present', 'pass', 'failure'),
    (Child, 'method', 'pass', 'failure'),
    (Child, 'absent', 'failure', 'pass'),
    (unittest, 'TestCase', 'pass', 'failure'),
    (unittest, 'absent', 'failure', 'pass'),
    (None, 'absent', 'failure', 'pass'),
    (1, 'absent', 'failure', 'pass'),
    (instance, '', 'failure', 'pass'),
    (instance, 42, 'TypeError', 'TypeError'),
    (instance, None, 'TypeError', 'TypeError'),
    (instance, b'own', 'TypeError', 'TypeError'),
]

for receiver in (case, custom):
    for entry in attributes:
        for assertion in (('assertHasAttr', entry[2]),
                          ('assertNotHasAttr', entry[3])):
            name = assertion[0]
            expected = assertion[1]
            method = getattr(unittest.TestCase, name)
            assert outcome(method, receiver, (entry[0], entry[1]), {}) == expected
            assert outcome(method, receiver, (),
                           {'obj': entry[0], 'name': entry[1],
                            'msg': 'message'}) == expected
            count += 2


# Each API rejects bad binding before evaluating its body, even when the
# supplied operands would otherwise pass.
for entry in (
    ('assertIsSubclass', (Child, Parent), {'cls': Child, 'superclass': Parent}),
    ('assertNotIsSubclass', (Child, Other), {'cls': Child, 'superclass': Other}),
    ('assertHasAttr', (instance, 'own'), {'obj': instance, 'name': 'own'}),
    ('assertNotHasAttr', (instance, 'absent'), {'obj': instance, 'name': 'absent'}),
):
    name = entry[0]
    args = entry[1]
    keywords = entry[2]
    method = getattr(unittest.TestCase, name)
    assert method(self=case, **keywords) is None
    count += 1
    for binding in (
        ((), {}), ((case,), {}), ((case, args[0]), {}),
        ((case,) + args + (None, None), {}),
        ((case,) + args, {'unexpected': 1}),
        ((case,) + args, {'self': case}),
        ((case,) + args, keywords),
        ((case,) + args + (None,), {'msg': None}),
    ):
        supplied = binding[0]
        kw = binding[1]
        try:
            method(*supplied, **kw)
        except TypeError:
            pass
        else:
            assert False
        count += 1


# An override of fail is allowed to return. Normal predicate failures then
# return None, but nonclass-operand TypeErrors are reraised after fail returns.
events = []


class ReturningCase(Case):
    def fail(self, msg=None):
        events.append('fail')


returning = ReturningCase('test_empty')
for entry in (
    ('assertIsSubclass', (Parent, Child), 'pass'),
    ('assertNotIsSubclass', (Child, Parent), 'pass'),
    ('assertHasAttr', (instance, 'absent'), 'pass'),
    ('assertNotHasAttr', (instance, 'own'), 'pass'),
    ('assertIsSubclass', (1, Parent), 'TypeError'),
    ('assertNotIsSubclass', (1, Parent), 'TypeError'),
):
    name = entry[0]
    args = entry[1]
    expected = entry[2]
    before = len(events)
    method = getattr(unittest.TestCase, name)
    assert outcome(method, returning, args, {'msg': name}) == expected
    assert events[before:] == ['fail']
    count += 1


# Direct method calls preserve receiver binding. Stored class contents and
# shapes can change after repeated access at the same assertion call site.
def present(obj, name):
    return case.assertHasAttr(obj, name)


def absent(obj, name):
    return case.assertNotHasAttr(obj, name)


def call(obj):
    return obj.method()


for i in range(30):
    assert present(instance, 'method') is None
    assert absent(instance, 'new') is None
    assert call(instance) == 1


def changed(self):
    return 2


Parent.method = changed
Parent.new = None
for i in range(30):
    assert call(instance) == 2
    assert present(instance, 'method') is None
    assert present(instance, 'new') is None
del Parent.method
del Parent.new
for i in range(30):
    assert absent(instance, 'method') is None
    assert absent(instance, 'new') is None
instance.method = None
assert present(instance, 'method') is None
del instance.method
assert absent(instance, 'method') is None
unittest.assertion_probe = None
assert present(unittest, 'assertion_probe') is None
del unittest.assertion_probe
assert absent(unittest, 'assertion_probe') is None

print('unittest class/attribute outcomes:', count)
