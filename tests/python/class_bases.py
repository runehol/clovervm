class Base:
    value = 7


class Derived(Base):
    pass


assert Derived.value == 7
assert Derived.__bases__[0] is Base


class Plain:
    pass


assert Plain.__bases__[0] is object


class ExplicitObject(object):
    pass


assert ExplicitObject.__mro__[1] is object


class Left:
    marker = 1
    value = 7


class Right:
    marker = 2
    value = 8


class Multi(Left, Right):
    pass


assert Multi.__bases__[1].marker == 2
assert Multi.value == 7


class ObjectAfterBase(Base, object):
    pass


assert ObjectAfterBase.__mro__[1] is Base


class Top:
    value = 1
    marker = 1


class DiamondLeft(Top):
    marker = 2


class DiamondRight(Top):
    value = 2
    marker = 3


class Bottom(DiamondLeft, DiamondRight):
    pass


assert Bottom.value == 2
mro_markers = Bottom.__mro__[1].marker * 100 + Bottom.__mro__[2].marker * 10 + Bottom.__mro__[3].marker
assert mro_markers == 231
