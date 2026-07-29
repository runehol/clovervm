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
