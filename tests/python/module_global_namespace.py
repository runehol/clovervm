from tests.python.support.module_global_provider import (
    read_builtin_probe,
    read_value,
)


value = 22
builtin_probe = 99

assert read_value() == 11
assert read_builtin_probe() == 31

# A module accepts reassignment to its current compatible module class.
builtins_module = __builtins__
builtins_module_class = builtins_module.__class__
builtins_module.__class__ = builtins_module_class
assert builtins_module.__class__ is builtins_module_class

import sys

sys.local_builtin_probe = 123
__builtins__ = sys
assert local_builtin_probe == 123

__builtins__ = 1
del __builtins__
assert len((1, 2, 3)) == 3

# Module namespace views count only visible bindings.
globals()["length_probe_x"] = 1
globals_length_before = len(globals())
globals()["length_probe_y"] = 2
assert len(globals()) - globals_length_before == 2

# Module namespace repr uses dict syntax and handles self-reference.
globals_repr_probe = 1
globals_text = repr(globals())
assert globals_text[0] == "{"
assert globals_text[-1] == "}"
assert "'globals_repr_probe': 1" in globals_text

globals_self_reference = globals()
assert "'globals_self_reference': {...}" in repr(globals_self_reference)

# globals and locals views write through to current module bindings.
globals()["written_through_view"] = 42
assert written_through_view == 42

globals()["range"] = 42
del globals()["range"]
assert next(range(1)) == 0

locals_x = 1
locals()["locals_y"] = 2
assert locals_x + locals_y == 3

assert globals().__class__.__name__ == "slotdict"
