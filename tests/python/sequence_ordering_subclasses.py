# Shared CPython 3.14.5 / CloverVM semantic regression coverage.
def lt(a, b):
    return a < b


def le(a, b):
    return a <= b


def gt(a, b):
    return a > b


def ge(a, b):
    return a >= b


def changed(self, other):
    return 'changed'


# Inherited methods read stored payloads. Normal/reflected protocol dispatch
# must still honor subclass overrides, including after warmed-site mutation.
for sequence in (list, tuple):
    class PayloadSubclass(sequence):
        def __len__(self):
            raise AssertionError('native payload length expected')

        def __getitem__(self, index):
            raise AssertionError('native payload item expected')

    class InheritedPayload(PayloadSubclass):
        pass

    for i in range(3):
        assert lt(InheritedPayload((1,)), sequence((2,))) is True
        assert gt(sequence((2,)), InheritedPayload((1,))) is True

    class Override(sequence):
        def __lt__(self, other):
            return 'sub lt'

        def __le__(self, other):
            return 'sub le'

        def __gt__(self, other):
            return 'sub gt'

        def __ge__(self, other):
            return 'sub ge'

    for i in range(3):
        assert lt(sequence(), Override()) == 'sub gt'
        assert le(sequence(), Override()) == 'sub ge'
        assert gt(sequence(), Override()) == 'sub lt'
        assert ge(sequence(), Override()) == 'sub le'
        assert lt(Override(), sequence()) == 'sub lt'
        assert le(Override(), sequence()) == 'sub le'
        assert gt(Override(), sequence()) == 'sub gt'
        assert ge(Override(), sequence()) == 'sub ge'

    Override.__gt__ = changed
    assert lt(sequence(), Override()) == 'changed'
    del Override.__gt__
    assert lt(sequence(), Override((1,))) is True

    # Explicit builtin calls bypass subclass special-method overrides.
    assert sequence.__lt__(Override((1,)), sequence((2,))) is True
    assert sequence.__gt__(sequence((2,)), Override((1,))) is True
