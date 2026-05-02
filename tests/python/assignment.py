a = 4
assert a + 3 == 7

a += 7
assert a == 11

value: int
assert 1 == 1

value: int = 7
assert value == 7

marker = 0

class AnnotationReceiver:
    pass

def side():
    global marker
    marker = 1
    return AnnotationReceiver()

side().missing: int
assert marker == 1

marker = 0

def side_list():
    global marker
    marker = 1
    return []

def key():
    global marker
    marker = marker + 2
    return 0

side_list()[key()]: int
assert marker == 3
