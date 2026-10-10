import math


assert math.nextafter(1.0, 2.0, steps=True) == math.nextafter(1.0, 2.0)
assert math.nextafter(1.0, 2.0, steps=False) == 1.0
assert math.nextafter(1.0, 2.0, steps=0) == 1.0
assert math.nextafter(1.0, 2.0, steps=2) == math.nextafter(math.nextafter(1.0, 2.0), 2.0)

for steps in (2.0, -1.5, "2", []):
    raised = False
    try:
        math.nextafter(1.0, 2.0, steps=steps)
    except TypeError:
        raised = True
    assert raised

for steps in (-1, -(10**30)):
    raised = False
    try:
        math.nextafter(1.0, 2.0, steps=steps)
    except ValueError:
        raised = True
    assert raised

# Custom __index__ conversion remains outside the builtin-only scope.
class IndexSteps:
    def __index__(self):
        return 1


raised = False
try:
    math.nextafter(1.0, 2.0, steps=IndexSteps())
except TypeError:
    raised = True
assert raised
