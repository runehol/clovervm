# Membership falls back through iteration and the indexed sequence protocol.
assert 1 in [1, 2]
assert 3 not in [1, 2]


class IndexedSequence:
    def __getitem__(self, idx):
        if idx == 0:
            return "a"
        if idx == 1:
            return "b"
        raise IndexError


assert "b" in IndexedSequence()


class StopIterationSequence:
    def __getitem__(self, idx):
        if idx == 0:
            return "a"
        raise StopIteration


assert "b" not in StopIterationSequence()


# __contains__ takes priority, truth-tests its result, and observes later installation.
class Container:
    def __contains__(self, item):
        return item == 7


assert 7 in Container()
assert 8 not in Container()


class MutableContainer:
    def __iter__(self):
        return iter(())


def contains(container, needle):
    return needle in container


mutable_container = MutableContainer()
assert 1 not in mutable_container


def always_contains(self, needle):
    return True


MutableContainer.__contains__ = always_contains
assert contains(mutable_container, 1)


class FalseContains:
    def __contains__(self, item):
        return 0


class NotImplementedContains:
    def __contains__(self, item):
        return NotImplemented


assert 1 not in FalseContains()
assert 1 in NotImplementedContains()

# Native container methods remain directly visible.
assert {"x": 1}.__contains__("x")
assert not {"x": 1}.__contains__("y")
assert "abc".__contains__("b")
assert not "abc".__contains__("z")
assert "abc".__contains__("bc")


# Sequence membership compares each item with the needle.
class MatchesNeedle:
    def __eq__(self, other):
        return other == 2


assert MatchesNeedle() in [1, 2]
assert MatchesNeedle() in (1, 2)
