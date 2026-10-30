# Original regressions checked against CPython 3.14.5, Objects/bytearrayobject.c.

def check_insert_and_pop():
    for case in [(-9223372036854775808, b'Xabc'),
                 (9223372036854775807, b'abcX'),
                 (-4, b'Xabc'), (-3, b'Xabc'), (-1, b'abXc'),
                 (0, b'Xabc'), (2, b'abXc'), (3, b'abcX'),
                 (4, b'abcX')]:
        index = case[0]
        expected = case[1]
        a = bytearray(b'abc')
        alias = a
        assert a.insert(index, 88) is None
        assert a == expected and alias == expected
    a = bytearray()
    assert a.insert(-1, 255) is None and a == b'\xff'
    assert a.pop() == 255 and a == b''
    a.insert(True, False)
    a.insert(False, True)
    assert a == b'\x01\x00'
    assert a.pop(True) == 0 and a.pop(False) == 1
    for case in [(0, 97, b'bcd'), (1, 98, b'acd'),
                 (-1, 100, b'abc'), (-4, 97, b'bcd')]:
        index = case[0]
        result = case[1]
        expected = case[2]
        a = bytearray(b'abcd')
        alias = a
        assert a.pop(index) == result and a == expected and alias == expected
    a = bytearray(range(256))
    for i in range(255, -1, -1):
        assert a.pop() == i
    assert a == b''
    # Exercise backing growth while inserting at both ends and in the middle.
    a = bytearray()
    for i in range(256):
        a.insert(0, i)
    assert a == bytearray(range(255, -1, -1))
    for i in range(256):
        a.insert(len(a) // 2, i)
    assert len(a) == 512


def check_remove_reverse_copy():
    a = bytearray([0, 255, 0, 255])
    alias = a
    assert a.remove(255) is None and a == b'\x00\x00\xff'
    assert alias == a
    assert a.remove(False) is None and a == b'\x00\xff'
    assert a.remove(0) is None and a.remove(255) is None and a == b''
    for source in [b'', b'x', b'ab', b'abc', bytes(bytearray(range(256)))]:
        a = bytearray(source)
        alias = a
        b = a.copy()
        assert isinstance(b, bytearray) and b is not a and b == a
        assert a.reverse() is None and a == source[::-1] and alias == a
        assert b == source
        b.append(88)
        assert len(b) == len(a) + 1
        a.clear()
        assert b == source + b'X'
        b.clear()
        a.append(89)
        assert b == b'' and a == b'Y'


def check_errors():
    huge = 10 ** 100
    for source in [b'', b'abc']:
        for index in [huge, -huge, 9223372036854775808, -9223372036854775809]:
            a = bytearray(source)
            try:
                a.pop(index)
            except OverflowError:
                pass
            else:
                raise AssertionError
            for byte in [65, 999, 'bad']:
                try:
                    a.insert(index, byte)
                except OverflowError:
                    pass
                else:
                    raise AssertionError
            assert a == source
        for index in [None, 'bad', 1.0, b'0']:
            a = bytearray(source)
            try:
                a.pop(index)
            except TypeError:
                pass
            else:
                raise AssertionError
            try:
                a.insert(index, 999)
            except TypeError:
                pass
            else:
                raise AssertionError
            assert a == source
        for index in [-9223372036854775808, -4, 3, 9223372036854775807]:
            a = bytearray(source)
            try:
                a.pop(index)
            except IndexError:
                pass
            else:
                raise AssertionError
            assert a == source
        for byte in [-1, 256, huge, -huge]:
            a = bytearray(source)
            for operation in [0, 1]:
                try:
                    if operation == 0:
                        a.insert(0, byte)
                    else:
                        a.remove(byte)
                except ValueError:
                    pass
                else:
                    raise AssertionError
                assert a == source
        for byte in [None, 1.0, b'a', bytearray(b'a')]:
            a = bytearray(source)
            for operation in [0, 1]:
                try:
                    if operation == 0:
                        a.insert(0, byte)
                    else:
                        a.remove(byte)
                except TypeError:
                    pass
                else:
                    raise AssertionError
                assert a == source
        a = bytearray(source)
        try:
            a.remove(255)
        except ValueError:
            pass
        else:
            raise AssertionError
        assert a == source
    try:
        bytearray().pop()
    except IndexError:
        pass
    else:
        raise AssertionError


def check_integer_subclasses():
    events = []
    a = bytearray(b'abc')
    class Integer(int):
        def __index__(self):
            events.append('index')
            a.clear()
            raise RuntimeError
        def __eq__(self, other):
            events.append('eq')
            a.clear()
            raise RuntimeError
    assert a.insert(Integer(-1), Integer(98)) is None and a == b'abbc'
    assert a.pop(Integer(0)) == 97 and a == b'bbc'
    assert a.remove(Integer(98)) is None and a == b'bc'
    assert events == []
    try:
        a.pop(Integer(10 ** 100))
    except OverflowError:
        pass
    else:
        raise AssertionError
    try:
        a.remove(Integer(256))
    except ValueError:
        pass
    else:
        raise AssertionError
    assert events == [] and a == b'bc'


def check_iterator_mutation():
    a = bytearray(b'abcd')
    it = iter(a)
    assert next(it) == 97
    a.insert(0, 88)
    assert next(it) == 97
    a.pop(0)
    assert next(it) == 99
    a.remove(98)
    try:
        next(it)
    except StopIteration:
        pass
    else:
        raise AssertionError
    a.insert(0, 89)
    a.reverse()
    a.extend(range(256))
    try:
        next(it)
    except StopIteration:
        pass
    else:
        raise AssertionError
    a = bytearray(b'abc')
    it = iter(a)
    assert next(it) == 97
    a.reverse()
    assert next(it) == 98
    a.copy().reverse()
    assert next(it) == 97
    assert a == b'cba'
    # Removing the last returned value does not rewind an iterator's cursor.
    a = bytearray(b'abc')
    it = iter(a)
    assert next(it) == 97
    a.remove(97)
    assert next(it) == 99


# Module-level callbacks avoid depending on enclosing-scope lookup in nested
# functions; the bytearray argument-evaluation assertions are unchanged.
def alias_index():
    argument_events.append('index')
    argument_target.append(88)
    return -1


def alias_byte():
    argument_events.append('byte')
    argument_target.pop(0)
    return 89


def alias_removable():
    argument_target.reverse()
    return 89


def alias_position():
    argument_target.insert(0, 90)
    return 1


def check_argument_alias_mutation():
    global argument_target, argument_events
    argument_target = bytearray(b'abc')
    argument_events = []
    argument_target.insert(alias_index(), alias_byte())
    assert argument_events == ['index', 'byte'] and argument_target == b'bcYX'
    argument_target.remove(alias_removable())
    assert argument_target == b'Xcb'
    assert argument_target.pop(alias_position()) == 88
    assert argument_target == b'Zcb'


check_insert_and_pop()
check_remove_reverse_copy()
check_errors()
check_integer_subclasses()
check_iterator_mutation()
check_argument_alias_mutation()

# Each public method rejects keywords, wrong arity, and an invalid receiver.
a = bytearray(b'a')
for operation in range(20):
    try:
        if operation == 0:
            a.pop(index=0)
        elif operation == 1:
            a.insert(index=0, item=1)
        elif operation == 2:
            a.remove(value=97)
        elif operation == 3:
            a.reverse(1)
        elif operation == 4:
            a.copy(1)
        elif operation == 5:
            a.pop(0, 1)
        elif operation == 6:
            a.insert(0)
        elif operation == 7:
            a.insert(0, 1, 2)
        elif operation == 8:
            a.remove()
        elif operation == 9:
            a.remove(1, 2)
        elif operation == 10:
            bytearray.pop(object())
        elif operation == 11:
            bytearray.insert(object(), 0, 1)
        elif operation == 12:
            bytearray.remove(object(), 1)
        elif operation == 13:
            bytearray.reverse(object())
        elif operation == 14:
            bytearray.copy(object())
        elif operation == 15:
            a.reverse(unexpected=1)
        elif operation == 16:
            a.copy(unexpected=1)
        elif operation == 17:
            a.insert()
        elif operation == 18:
            bytearray.pop(object(), 10 ** 100)
        else:
            bytearray.insert(object(), 10 ** 100, 999)
    except TypeError:
        pass
    else:
        raise AssertionError
    assert a == b'a'
