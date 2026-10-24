# Hierarchy and ordinary-constructor cases checked against local CPython 3.14.5
# and CPython v3.14.5 Objects/exceptions.c. These are CloverVM regression tests.
import builtins

relationships = [
    (ArithmeticError, Exception),
    (FloatingPointError, ArithmeticError),
    (OverflowError, ArithmeticError),
    (ZeroDivisionError, ArithmeticError),
    (LookupError, Exception),
    (IndexError, LookupError),
    (KeyError, LookupError),
    (SyntaxError, Exception),
    (IndentationError, SyntaxError),
    (TabError, IndentationError),
    (BufferError, Exception),
    (EOFError, Exception),
    (MemoryError, Exception),
    (ReferenceError, Exception),
    (RuntimeError, Exception),
    (RecursionError, RuntimeError),
    (PythonFinalizationError, RuntimeError),
    (Warning, Exception),
    (BytesWarning, Warning),
    (DeprecationWarning, Warning),
    (EncodingWarning, Warning),
    (FutureWarning, Warning),
    (ImportWarning, Warning),
    (PendingDeprecationWarning, Warning),
    (ResourceWarning, Warning),
    (RuntimeWarning, Warning),
    (SyntaxWarning, Warning),
    (UnicodeWarning, Warning),
    (UserWarning, Warning),
]

for relationship in relationships:
    cls = relationship[0]
    parent = relationship[1]
    name = cls.__name__
    assert getattr(builtins, name) is cls
    assert cls.__bases__ == (parent,)
    assert issubclass(cls, parent)
    assert issubclass(cls, Exception)
    assert issubclass(cls, BaseException)
    for args in [(), ('message',), ('message', 7)]:
        error = cls(*args)
        assert error.__class__ is cls
        assert isinstance(error, parent)
        assert error.args == args
        # Existing generic formatting is intentional for KeyError and syntax
        # errors too; their specialized CPython behavior is a documented gap.
        if len(args) == 0:
            assert str(error) == ''
            assert repr(error) == name + '()'
        elif len(args) == 1:
            assert str(error) == 'message'
            assert repr(error) == name + "('message')"
        else:
            assert str(error) == "('message', 7)"
            assert repr(error) == name + "('message', 7)"
        caught = False
        try:
            raise error
        except parent as raised:
            assert raised is error
            caught = True
        assert caught
    caught = False
    try:
        raise cls
    except parent as raised:
        assert raised.__class__ is cls
        assert raised.args == ()
        caught = True
    assert caught
    try:
        cls(message='unsupported')
    except TypeError:
        pass
    else:
        assert False

    # Every registered class remains a valid base with the native args layout.
    class Derived(cls):
        pass

    error = Derived('derived')
    error.note = 42
    assert error.note == 42
    assert error.args == ('derived',)
    assert str(error) == 'derived'
    assert repr(error) == "Derived('derived')"
    try:
        raise error
    except cls as raised:
        assert raised is error
    else:
        assert False

# Siblings and their descendants must not match one another.
for pair in [
    (FloatingPointError, ZeroDivisionError),
    (OverflowError, FloatingPointError),
    (IndexError, KeyError),
    (RecursionError, PythonFinalizationError),
    (PythonFinalizationError, NotImplementedError),
    (TabError, LookupError),
    (UserWarning, RuntimeWarning),
    (RuntimeWarning, RuntimeError),
    (SyntaxWarning, SyntaxError),
]:
    cls = pair[0]
    sibling = pair[1]
    assert not issubclass(cls, sibling)
    try:
        raise cls('sibling')
    except sibling:
        assert False
    except Exception as error:
        assert error.__class__ is cls

# Existing producers must now be catchable through the added parent classes.
for operation in ['division', 'key', 'index']:
    caught = False
    try:
        if operation == 'division':
            value = 1 / 0
        elif operation == 'key':
            value = {}['missing']
        else:
            value = [][0]
    except ArithmeticError as error:
        assert operation == 'division'
        assert error.__class__ is ZeroDivisionError
        caught = True
    except LookupError as error:
        if operation == 'key':
            assert error.__class__ is KeyError
        else:
            assert operation == 'index'
            assert error.__class__ is IndexError
        caught = True
    assert caught

assert KeyboardInterrupt.__bases__ == (BaseException,)
assert not issubclass(KeyboardInterrupt, Exception)
try:
    raise KeyboardInterrupt('interrupt')
except Exception:
    assert False
except BaseException as error:
    assert error.__class__ is KeyboardInterrupt
    assert error.args == ('interrupt',)
