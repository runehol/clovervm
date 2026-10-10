# Native exceptions expose their argument tuples before constructors are added.
try:
    assert False, "boom"
except AssertionError as error:
    assert len(error.args) == 1
    assert error.args[0] == "boom"
    assert error.__str__() == "boom"
    assert repr(error) == "AssertionError('boom')"
    assert not hasattr(error, "message")
    try:
        error.args = ()
    except AttributeError:
        pass
    else:
        assert False
    try:
        error.__dict__["args"] = ()
    except TypeError:
        pass
    else:
        assert False
    try:
        del error.args
    except AttributeError:
        pass
    else:
        assert False

try:
    raise ValueError
except ValueError as error:
    assert len(error.args) == 0
    assert error.__str__() == ""
    assert repr(error) == "ValueError()"

try:
    next(iter(()))
except StopIteration as error:
    assert len(error.args) == 0
    assert error.value is None
    assert error.__str__() == ""
    assert repr(error) == "StopIteration()"

try:
    raise StopIteration
except StopIteration as error:
    assert len(error.args) == 0
    assert error.value is None
