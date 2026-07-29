def add3(a, b, c):
    return a + b + c

assert add3(1, 2, 3) == 6

def add4(a, b, c, d):
    return a + b + c + d

assert add4(1, 2, 3, 4) == 10

def add(a, b=10, c=100):
    return a + b + c

assert add(1) + add(1, 2) + add(1, 2, 3) == 220

x = 10

def default_from_definition_time(a=x + 1):
    return a

x = 20
assert default_from_definition_time() == 11

def varargs(a, *args):
    return a + args[0] + args[1]

assert varargs(1, 4, 5) == 10

def defaults_and_varargs(a, b=10, *args):
    return a + b + args[0] + args[1]

assert defaults_and_varargs(1, 20, 21, 23) == 65

def defaults_and_empty_varargs(a, b=10, *args):
    return a + b

assert defaults_and_empty_varargs(1) == 11

def add3_trailing(a, b, c,):
    return a + b + c

assert add3_trailing(1, 2, 3,) == 6

def implicit_none():
    a = 1

assert implicit_none() is None

def fun_w_str():
    "hello world"
    pass

def fun_wo_str():
    pass

assert fun_w_str.__doc__ == "hello world"
assert fun_wo_str.__doc__ is None
fun_wo_str.__doc__ = "later"
assert fun_wo_str.__doc__ == "later"

def pick(n):
    if n:
        while n:
            return 1
    else:
        return 2

assert pick(0) == 2

def fib(n):
    if n <= 2:
        return n
    return fib(n - 2) + fib(n - 1)

assert fib(20) == 10946


default_eval_order = 0

def record_default(n):
    global default_eval_order
    default_eval_order = default_eval_order * 10 + n
    return n

def keyword_only_default_holes(a=record_default(1), *, b, c=record_default(2)):
    return a * 100 + b * 10 + c

assert default_eval_order == 12
assert keyword_only_default_holes(b=3) == 132
assert keyword_only_default_holes(4, b=5) == 452

try:
    keyword_only_default_holes()
    missing_keyword_only_rejected = False
except TypeError:
    missing_keyword_only_rejected = True

assert missing_keyword_only_rejected

try:
    keyword_only_default_holes(4, 5)
    positional_keyword_only_rejected = False
except TypeError:
    positional_keyword_only_rejected = True

assert positional_keyword_only_rejected

assert keyword_only_default_holes(b=6) == 162
assert keyword_only_default_holes(b=7) == 172

def varargs_with_default_holes(a=1, *args, b, c=3):
    return a * 1000 + len(args) * 100 + b * 10 + c

assert varargs_with_default_holes(b=4) == 1043
assert varargs_with_default_holes(5, 6, 7, b=8) == 5283

def positional_and_keyword_defaults(a=1, *, b, c=3):
    return a * 100 + b * 10 + c

assert positional_and_keyword_defaults(b=2) == 123
assert positional_and_keyword_defaults(4, b=5) == 453

class ConstructorDefaultHoles:
    def __init__(self=0, a=1, *, b, c=3):
        self.value = a * 100 + b * 10 + c

assert ConstructorDefaultHoles(b=2).value == 123
assert ConstructorDefaultHoles(4, b=5).value == 453

class ConstructorVarargsDefaultHoles:
    def __init__(self=0, a=1, *args, b, c=3):
        self.value = a * 1000 + len(args) * 100 + b * 10 + c

assert ConstructorVarargsDefaultHoles(b=2).value == 1023
assert ConstructorVarargsDefaultHoles(4, 5, 6, b=7).value == 4273


# Function calls bind reordered keywords, defaults, varargs, and keyword-only arguments.
def reordered_keywords(a, b, c):
    return a * 100 + b * 10 + c


assert reordered_keywords(1, c=3, b=2) == 123


def cached_keywords(a, b):
    return a * 10 + b


assert cached_keywords(a=1, b=2) == 12
assert cached_keywords(a=3, b=4) == 34


def keyword_defaults(a, b=1, c=2, d=4):
    return a * 1000 + b * 100 + c * 10 + d


assert keyword_defaults(7, 8, d=9) == 7829


def required_before_default(a, b=2):
    return a * 10 + b


assert required_before_default(a=3) == 32


def keyword_call_with_varargs(a, b=2, *args):
    return a * 10 + b + len(args)


assert keyword_call_with_varargs(a=3) == 32


def keyword_only_defaults(a, *, b=2, c=3):
    return a * 100 + b * 10 + c


assert keyword_only_defaults(4, c=5) == 425


def varargs_before_keyword_only(*args, sep=9):
    return len(args) * 10 + sep


assert varargs_before_keyword_only(1, 2, sep=3) == 23


def required_keyword_only(a=1, *, b, c=3):
    return a * 100 + b * 10 + c


assert required_keyword_only(b=2) == 123


def positional_only(a, /):
    return a


assert positional_only(7) == 7


def positional_only_with_keyword(a, /, b):
    return a * 10 + b


assert positional_only_with_keyword(3, b=4) == 34


def positional_only_defaults(a=3, /, b=4):
    return a * 10 + b


assert positional_only_defaults() == 34


def positional_only_kwargs(a, /, **kwargs):
    return a * 10 + kwargs["a"]


assert positional_only_kwargs(3, a=4) == 34


def collect_empty_kwargs(**kwargs):
    return len(kwargs)


assert collect_empty_kwargs() == 0


def collect_ordered_kwargs(**kwargs):
    return str(kwargs)


assert collect_ordered_kwargs(c=3, a=1, b=2) == "{'c': 3, 'a': 1, 'b': 2}"


def collect_unmatched_kwargs(a, *, b=2, **kwargs):
    return a * 1000 + b * 100 + kwargs["c"] * 10 + kwargs["d"]


assert collect_unmatched_kwargs(1, d=4, b=3, c=2) == 1324


def fresh_kwargs(**kwargs):
    return kwargs


assert fresh_kwargs() is not fresh_kwargs()


def collect_args_and_kwargs(a, *args, **kwargs):
    return a * 100 + len(args) * 10 + kwargs["x"]


assert collect_args_and_kwargs(1, 2, 3, x=4) == 124


# An unused varargs parameter receives an empty tuple.
def collect_empty_varargs(*args):
    return args


assert len(collect_empty_varargs()) == 0
