# CloverVM-specific boundary: custom __index__ dispatch is unsupported.
class IndexOnly:
    def __index__(self):
        raise AssertionError("custom __index__ must not be invoked")

for fn in (bin, oct, hex):
    raised = False
    try:
        fn(IndexOnly())
    except TypeError:
        raised = True
    assert raised
