# CloverVM's tagged bytes count and stored-payload conversion limits.

class Index:
    def __index__(self):
        raise AssertionError("custom index conversion is excluded")


try:
    (0).to_bytes(Index())
    assert False
except TypeError:
    pass

# This is within signed 64-bit range, but above the positive tagged count.
too_large = 1 << 58
try:
    (-1).to_bytes(too_large)
    assert False
except OverflowError as error:
    assert str(error) == "byte string is too large"

calls = []


class Signed:
    def __len__(self):
        calls.append(1)
        return 1


try:
    (0).to_bytes(too_large, "bad", signed=Signed())
    assert False
except ValueError as error:
    assert str(error) == "byteorder must be either 'little' or 'big'"
assert calls == [1]
