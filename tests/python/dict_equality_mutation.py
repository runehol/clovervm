# Callback mutation follows live entry traversal and restarts stale probes.
class ClearValues:
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def __eq__(self, other):
        self.left.clear()
        self.right.clear()
        return True


left = {}
right = {}
left['a'] = ClearValues(left, right)
left['b'] = 2
right['a'] = 0
right['b'] = 3
assert left == right
assert len(left) == len(right) == 0


class LeftKey:
    def __hash__(self):
        return 42


class GrowingKey:
    def __init__(self, mapping):
        self.mapping = mapping
        self.calls = 0

    def __hash__(self):
        return 42

    def __eq__(self, other):
        self.calls += 1
        if self.calls == 1:
            for index in range(80):
                self.mapping[index + 100] = index
        return True


right = {}
key = GrowingKey(right)
right[key] = 1
left = {LeftKey(): 1}
assert left == right
assert key.calls == 2
assert len(right) == 81

# Removing the candidate during equality restarts the lookup and reports a miss.
class RemovingKey:
    def __hash__(self):
        return 42

    def __eq__(self, other):
        right.clear()
        return True


right = {RemovingKey(): 1}
assert not {LeftKey(): 1} == right
assert len(right) == 0

failure = ValueError('key comparison failed')


class FailingKey:
    def __hash__(self):
        return 42

    def __eq__(self, other):
        raise failure


try:
    {LeftKey(): 1} == {FailingKey(): 1}
    assert False
except ValueError as error:
    assert error is failure
