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
