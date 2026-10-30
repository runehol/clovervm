# Native conversions reject unsupported __index__ without executing callbacks.
class Index:
    def __index__(self):
        raise AssertionError
for method in (bytes.split, bytes.rsplit):
    raised = False
    try:
        method(b'a', None, Index())
    except TypeError:
        raised = True
    assert raised

