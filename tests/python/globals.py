a = 10

def local_shadow():
    a = 1
    return a

assert local_shadow() + a == 11

a = 10

def read_global():
    return a

assert read_global() == 10

a = 10

def assign_global():
    global a
    a = 12
    return a

assert assign_global() + a == 24

global annotated_after_global
annotated_after_global: int
assert 1 == 1

range = 42
assert range == 42
