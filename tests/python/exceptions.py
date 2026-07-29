result = 0
try:
    raise ValueError
    result = 1
except:
    result = 7
assert result == 7

result = 0
try:
    result = 3
except:
    result = 7
assert result == 3

result = 0
try:
    raise NameError
except NameError:
    result = 7
assert result == 7

result = 0
try:
    raise NameError
except Exception:
    result = 7
assert result == 7

result = 0
try:
    try:
        raise NameError
    except ValueError:
        result = 1
except:
    result = 7
assert result == 7

result = 0
try:
    raise NameError
except ValueError:
    result = 1
except NameError:
    result = 7
except:
    result = 2
assert result == 7

result = 0
try:
    raise TypeError
except ValueError:
    result = 1
except NameError:
    result = 2
except:
    result = 7
assert result == 7

result = 0
try:
    try:
        raise ValueError
    except:
        raise TypeError
except TypeError:
    result = 7
assert result == 7


# return, break, and continue interact with finally cleanup and override pending exits.
def return_overrides_exception():
    try:
        raise ValueError
    finally:
        return 7


assert return_overrides_exception() == 7

result = 0
for x in range(3):
    try:
        raise ValueError
    finally:
        result = 9
        break
assert result + x == 9

result = 0
for x in range(3):
    result += 1
    try:
        raise ValueError
    finally:
        continue
    result = 99
assert result == 3

result = 0


def return_runs_finally():
    global result
    try:
        return 1
    finally:
        result = 2


assert return_runs_finally() + result * 10 == 21


def finally_return_overrides_return():
    try:
        return 1
    finally:
        return 2


assert finally_return_overrides_return() == 2

result = 0
for x in range(3):
    try:
        break
    finally:
        result += 10
assert result + x == 10

result = 0
try:
    for x in range(3):
        try:
            break
        finally:
            result += 10
    result += 1
finally:
    result += 100
assert result == 111

result = 0
for x in range(3):
    try:
        continue
    finally:
        result += 1
    result = 99
assert result == 3

result = 0


# else and except returns still run finally, including nested cleanup order.
def return_from_else_runs_finally():
    global result
    try:
        pass
    except NameError:
        pass
    else:
        return 4
    finally:
        result = 5


assert return_from_else_runs_finally() + result * 10 == 54

result = 0


def return_from_except_runs_finally():
    global result
    try:
        raise NameError
    except NameError:
        return 6
    finally:
        result = 7


assert return_from_except_runs_finally() + result * 10 == 76

result = 0


def return_runs_nested_finally():
    global result
    try:
        try:
            return 1
        finally:
            result = result * 10 + 2
    finally:
        result = result * 10 + 3


assert return_runs_nested_finally() + result * 10 == 231

result = 0
try:
    raise NameError
except NameError:
    result = 1
finally:
    result += 2
assert result == 3

result = 0
try:
    result = 1
except NameError:
    result = 2
else:
    result += 4
assert result == 5

result = 0
try:
    raise NameError
except NameError:
    result = 2
else:
    result = 99
assert result == 2

result = 0
try:
    result = 1
except NameError:
    result = 2
else:
    result += 4
finally:
    result += 8
assert result == 13

# finally cleanup runs on normal completion and before reraised or replacement exceptions.
result = 0
try:
    result = 1
finally:
    result = 2
assert result == 2

result = 0
try:
    try:
        raise ValueError
    finally:
        result = 2
    cleanup_reraise_caught = False
except ValueError:
    cleanup_reraise_caught = True
assert cleanup_reraise_caught
assert result == 2

result = 0


def return_then_raise_from_finally():
    global result
    try:
        return 1
    finally:
        result += 1
        raise ValueError


try:
    return_then_raise_from_finally()
    return_cleanup_raised = False
except ValueError:
    return_cleanup_raised = True
assert return_cleanup_raised
assert result == 1

result = 0
try:
    try:
        raise ValueError
    except NameError:
        result = 1
    finally:
        result = 2
    unmatched_handler_raised = False
except ValueError:
    unmatched_handler_raised = True
assert unmatched_handler_raised
assert result == 2

result = 0
try:
    try:
        raise NameError
    except NameError:
        raise ValueError
    finally:
        result = 2
    handler_reraise_caught = False
except ValueError:
    handler_reraise_caught = True
assert handler_reraise_caught
assert result == 2

try:
    try:
        pass
    except ValueError:
        assert False
    else:
        raise ValueError
    else_exception_escaped = False
except ValueError:
    else_exception_escaped = True
assert else_exception_escaped

result = 0
try:
    try:
        pass
    except NameError:
        result = 1
    else:
        raise ValueError
    finally:
        result = 2
    else_cleanup_raised = False
except ValueError:
    else_cleanup_raised = True
assert else_cleanup_raised
assert result == 2

result = 0
try:
    raise NameError
except NameError as e:
    e = 7
    result = e
assert result == 7

result = 0
try:
    try:
        raise NameError
    except NameError as e:
        e = TypeError
        raise
except NameError:
    result = 7
assert result == 7

result = 0
try:
    try:
        raise NameError
    except NameError:
        raise
except NameError:
    result = 7
assert result == 7

result = 0
try:
    raise ValueError
except:
    try:
        raise
    except ValueError:
        result = 7
assert result == 7

result = 0
try:
    try:
        raise ValueError
    except:
        try:
            raise TypeError
        except TypeError:
            raise
except TypeError:
    result = 7
assert result == 7
