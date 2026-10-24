# Bisect callback/error probes checked against CPython 3.14.5.
# No CPython test code copied.

import bisect


def raises(error, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except error as caught:
        return caught
    raise AssertionError("expected exception")


failure = ValueError("callback failed")


def failing_key(value):
    raise failure


class FailingSequence:
    def __len__(self):
        raise failure

    def __getitem__(self, index):
        raise failure

    def insert(self, index, value):
        raise failure


class FailingComparison:
    def __lt__(self, other):
        raise failure


def callback_exceptions():
    for search in (bisect.bisect_left, bisect.bisect_right):
        assert raises(ValueError, search, FailingSequence(), 1) is failure
        assert raises(ValueError, search, FailingSequence(), 1, hi=1) is failure
        assert raises(ValueError, search, [1], 1, key=failing_key) is failure
        assert raises(ValueError, search, [FailingComparison()], FailingComparison()) is failure
        # A key is not called without a retrieved element.
        assert search([], 1, key=failing_key) == 0
        raises(TypeError, search, [1], 1, key=42)
    for insert in (bisect.insort_left, bisect.insort_right):
        assert raises(ValueError, insert, [], 1, key=failing_key) is failure
        assert raises(ValueError, insert, FailingSequence(), 1, hi=0) is failure
        data = [1]
        assert raises(ValueError, insert, data, 2, key=failing_key) is failure
        assert data == [1]
        raises(AttributeError, insert, (1, 3), 2)


data = []
events = []


def clearing_key(value):
    events.append(value)
    data.clear()
    return value


def appending_key(value):
    events.append(value)
    data.append(9)
    return value


def replacing_key(value):
    events.append(value)
    data[0] = 4
    return value


class ClearingComparison:
    def __lt__(self, other):
        data.clear()
        return False


def mutation():
    global data
    for search in (bisect.bisect_left, bisect.bisect_right):
        data = [1, 3, 5]
        events.clear()
        raises(IndexError, search, data, 3, key=clearing_key)
        assert data == []
        assert events == [3]
    data = [1, 3, 5]
    events.clear()
    assert bisect.bisect_left(data, 3, key=replacing_key) == 0
    assert events == [3, 4]
    assert data == [4, 3, 5]
    for insert in (bisect.insort_left, bisect.insort_right):
        data = [1, 5]
        events.clear()
        assert insert(data, 3, key=appending_key) is None
        assert data == [1, 3, 5, 9, 9, 9]
        assert events == [3, 5, 1]
        data = [1, 5]
        events.clear()
        assert insert(data, 3, key=clearing_key) is None
        assert data == [3]
        assert events == [3]
    # Clearing during the final comparison is allowed; insertion uses the
    # returned index against the now-current list, not a retained backing array.
    data = [ClearingComparison()]
    assert bisect.insort_left(data, ClearingComparison()) is None
    assert len(data) == 1
    data = [ClearingComparison(), ClearingComparison(), ClearingComparison()]
    raises(IndexError, bisect.bisect_left, data, ClearingComparison())
    assert data == []


class Reflected:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return NotImplemented

    def __gt__(self, other):
        return self.value > other.value


class Truth:
    def __init__(self, result):
        self.result = result

    def __bool__(self):
        return self.result


class TruthComparison:
    def __init__(self, value):
        self.value = value

    def __lt__(self, other):
        return Truth(self.value < other.value)


def comparison_results():
    # Check fallback results, not the C accelerator's repeated __lt__ call.
    assert bisect.bisect_left([Reflected(1), Reflected(3)], Reflected(3)) == 1
    assert bisect.bisect_right([Reflected(1), Reflected(3)], Reflected(3)) == 2
    assert bisect.bisect_left([TruthComparison(1), TruthComparison(3)], TruthComparison(3)) == 1
    assert bisect.bisect_right([TruthComparison(1), TruthComparison(3)], TruthComparison(3)) == 2


callback_exceptions()
mutation()
comparison_results()
