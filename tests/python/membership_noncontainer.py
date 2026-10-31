# Independent terminal membership errors, checked against CPython 3.14.10.
class NoMembership:
    pass

for container in (None, 42, NoMembership()):
    for repeat in range(3):
        for needle in (0, 32, None):
            try:
                needle in container
            except TypeError as error:
                assert error.__class__ is TypeError
            else:
                raise AssertionError('noncontainer accepted by in')
            try:
                needle not in container
            except TypeError as error:
                assert error.__class__ is TypeError
            else:
                raise AssertionError('noncontainer accepted by not in')
# An earlier missing-protocol error must not hide a subsequently added method.
container = NoMembership()
def contains(self, needle):
    return needle == 32
NoMembership.__contains__ = contains
for repeat in range(3):
    assert 32 in container
    assert 0 not in container
