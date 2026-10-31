# Accepted native boundary: no general __index__ or buffer callbacks.
class Index:
    def __index__(self):
        raise AssertionError

for method in (bytearray.split, bytearray.rsplit):
    raised = False
    try:
        method(bytearray(b'a'), b':', Index())
    except TypeError:
        raised = True
    assert raised
