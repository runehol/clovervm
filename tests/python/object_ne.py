# Inherited object.__ne__ calls only the receiver's special __eq__, then
# truth-converts its result unless it is NotImplemented.
a = object()
b = object()
assert object.__ne__(a, a) is False
assert object.__ne__(a, b) is NotImplemented
assert (a != a) is False
assert (a != b) is True
assert object.__ne__(None, None) is False
assert object.__ne__(1, 1) is False
assert object.__ne__(1, 2) is True

class Equal:
    def __eq__(self, other):
        return self.result

a = Equal()
b = Equal()
for result in [False, True, None, 0, 1, 0.0, 1.5, 2 ** 100, -(2 ** 100),
               '', 'x', [], [1], (), (1,), {}, {'x': 1}, object()]:
    a.result = result
    expected = not result
    assert object.__ne__(a, b) is expected
    assert (a != b) is expected

a.result = True
def shadow(other):
    raise AssertionError('instance shadow called')
a.__eq__ = shadow
assert (a != b) is False
assert object.__ne__(a, b) is False
assert (a == b) is True

class BoolResult:
    def __bool__(self):
        return self.value
class LenResult:
    def __len__(self):
        return self.value
for cls in [BoolResult, LenResult]:
    result = cls()
    a.result = result
    for value in [False, True] if cls is BoolResult else [0, 3]:
        result.value = value
        assert object.__ne__(a, b) is (not value)
        assert (a != b) is (not value)

class Declines:
    def __eq__(self, other):
        return NotImplemented
x = Declines()
y = Declines()
assert object.__ne__(x, y) is NotImplemented
assert (x != y) is True
assert (x != x) is False

# Direct object.__ne__ must not run the full reflected equality protocol.
events = []
class Left:
    def __eq__(self, other):
        events.append('left eq')
        return NotImplemented
class Right:
    def __eq__(self, other):
        events.append('right eq')
        return True
    def __ne__(self, other):
        events.append('right ne')
        return 'reflected ne'
x = Left()
y = Right()
assert object.__ne__(x, y) is NotImplemented
assert events == ['left eq']
events.clear()
assert (x != y) == 'reflected ne'
assert events == ['left eq', 'right ne']
class Child(Left):
    def __eq__(self, other):
        events.append('child eq')
        return []
events.clear()
assert (x != Child()) is True
assert events == ['child eq']
