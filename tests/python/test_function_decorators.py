events = []
f = "old"


def decorator(label):
    events.append(("eval", label, f))

    def apply(value):
        events.append(("apply", label, f))
        if label == "bottom":
            assert value.__doc__ == "Decorated function."
            return value()
        return value + 1

    return apply


def default(label):
    events.append(label)
    return 3


@decorator("top")
@decorator("bottom")
def f(x=default("positional"), *, y=default("keyword-only")):
    """Decorated function."""
    return x + y


assert f == 7
assert events == [
    ("eval", "top", "old"),
    ("eval", "bottom", "old"),
    "positional",
    "keyword-only",
    ("apply", "bottom", "old"),
    ("apply", "top", "old"),
]


def failed_definition(stage):
    trace = []
    target = "old"

    def select(label):
        trace.append(("eval", label))
        if stage == "expression" and label == "bottom":
            raise ValueError()
        if stage == "application" and label == "bottom":
            return None

        def apply(function):
            trace.append(("apply", label))
            return function

        return apply

    def evaluate_default():
        trace.append("default")
        if stage == "default":
            raise ValueError()
        return 0

    try:
        @select("top")
        @select("bottom")
        def target(x=evaluate_default()):
            return x
    except TypeError:
        assert stage == "application"
        return target, trace
    except ValueError:
        assert stage != "application"
        return target, trace
    assert False


evaluations = [("eval", "top"), ("eval", "bottom")]
assert failed_definition("expression") == ("old", evaluations)
assert failed_definition("default") == ("old", evaluations + ["default"])
assert failed_definition("application") == ("old", evaluations + ["default"])


def failed_first_binding():
    try:
        @reject := None
        def unbound():
            pass
    except TypeError:
        assert reject is None
        try:
            unbound
        except UnboundLocalError:
            return True
    return False


assert failed_first_binding()


def enclosing():
    def identity(function):
        return function

    def defining():
        captured = 41

        @selected := identity
        def nested(x=selected(1)):
            return captured + x

        assert selected is identity
        return nested

    return defining()


nested = enclosing()
assert nested() == 42


class Methods:
    static_alias = staticmethod

    @staticmethod
    def static(value):
        return value + 1

    @static_alias
    def alias(value):
        return value

    @classmethod
    def class_bound(cls, value):
        return cls, value


class Derived(Methods):
    pass


obj = Derived()
static_from_class = Derived.static
static_from_instance = obj.static
bound_from_class = Derived.class_bound
bound_from_instance = obj.class_bound
assert static_from_class is static_from_instance
assert static_from_class(41) == 42
assert static_from_instance(41) == 42
alias_from_instance = obj.alias
assert alias_from_instance(42) == 42
assert bound_from_class(42) == (Derived, 42)
assert bound_from_instance(42) == (Derived, 42)


class Decorator:
    def replace(self, function):
        return 42


dec = Decorator()


def shadow_builtin():
    staticmethod = dec.replace

    @staticmethod
    def shadowed():
        pass

    return shadowed


assert shadow_builtin() == 42


@dec.replace
def replaced():
    pass


assert replaced == 42
