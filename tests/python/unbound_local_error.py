assert issubclass(UnboundLocalError, NameError)
assert issubclass(UnboundLocalError, Exception)
assert not issubclass(NameError, UnboundLocalError)
class DerivedUnboundLocalError(UnboundLocalError):
    pass
assert isinstance(DerivedUnboundLocalError('message'), UnboundLocalError)
assert isinstance(DerivedUnboundLocalError('message'), NameError)

def read_before_assignment():
    return value
    value = 1

def delete_missing_local():
    del value

def read_deleted_local():
    value = 1
    del value
    return value

def delete_local_twice():
    value = 1
    del value
    del value

def conditional_local():
    if False:
        value = 1
    return value

for operation in (read_before_assignment, delete_missing_local,
                  read_deleted_local, delete_local_twice, conditional_local):
    try:
        operation()
    except UnboundLocalError as error:
        assert error.__class__ is UnboundLocalError
        assert str(error) == "cannot access local variable 'value' where it is not associated with a value"
    else:
        assert False
    # Existing code catching NameError must continue to catch its new subclass.
    try:
        operation()
    except NameError as error:
        assert error.__class__ is UnboundLocalError
    else:
        assert False

def read_missing_global():
    return missing_global

def read_explicit_global():
    global missing_global
    return missing_global

def delete_explicit_global():
    global missing_global
    del missing_global

for operation in (read_missing_global, read_explicit_global, delete_explicit_global):
    try:
        operation()
    except UnboundLocalError:
        assert False
    except NameError as error:
        assert error.__class__ is NameError
        assert str(error) == "name 'missing_global' is not defined"
    else:
        assert False

try:
    missing_module_name
except UnboundLocalError:
    assert False
except NameError as error:
    assert error.__class__ is NameError
else:
    assert False

try:
    del missing_module_name
except UnboundLocalError:
    assert False
except NameError as error:
    assert error.__class__ is NameError
else:
    assert False
