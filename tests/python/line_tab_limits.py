# CloverVM-specific supported-payload boundary; custom index is excluded.
class Index:
    def __index__(self):
        raise AssertionError('general index conversion is excluded')
for cls in (str, bytes, bytearray):
    try:
        cls().expandtabs(Index())
    except TypeError:
        pass
    else:
        raise AssertionError('expected TypeError')
