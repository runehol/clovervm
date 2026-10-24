# Current CloverVM arithmetic boundary, not CPython-compatible behavior.
# Update these expectations when mixed heap-int/float addition is supported.
# count must delegate to the real arithmetic path, without eager addition or
# a module-local numeric conversion workaround.

from itertools import count


def raises_type_error(iterator):
    try:
        next(iterator)
    except TypeError:
        return
    raise AssertionError("expected the VM's mixed-number arithmetic error")


for case in ((1.0, 2**60), (2**60, 1.0), (1.0, 10**400), (10**400, 1.0)):
    values = count(case[0], case[1])  # Construction validates numeric types only.
    raises_type_error(values)
    raises_type_error(values)
