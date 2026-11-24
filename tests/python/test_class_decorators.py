events = []
C = "previous"


def make(label):
    events.append(("evaluate", label, C))

    def apply(value):
        events.append(("apply", label, C))
        if label == "inner":
            assert value.marker == 42
            return value.marker
        return value + 1

    return apply


def base():
    events.append("base")
    return object


@make("outer")
@make("inner")
class C(base()):
    events.append("body")
    marker = 42


assert events == [
    ("evaluate", "outer", "previous"),
    ("evaluate", "inner", "previous"),
    "base",
    "body",
    ("apply", "inner", "previous"),
    ("apply", "outer", "previous"),
]
assert C == 43


def register(cls):
    cls.registered = True
    return cls


def define_using_capture(decorator):
    def define():
        @selected := decorator
        class Local:
            pass

        assert selected is decorator
        return Local

    return define()


assert define_using_capture(register).registered


class Owner:
    register_nested = register

    @register_nested
    class Nested:
        pass


assert Owner.Nested.registered

failure_events = []
fail_in_body = False


def failed_definition(stage):
    global fail_in_body
    fail_in_body = stage == "body"
    failure_events.clear()

    def step(label):
        failure_events.append(label)
        if stage == label:
            raise ValueError(label)

    def select():
        step("evaluate")

        def apply(cls):
            step("apply")
            return cls

        return apply

    def select_base():
        step("base")
        return object

    Target = "previous"
    try:
        @select()
        class Target(select_base()):
            failure_events.append("body")
            if fail_in_body:
                raise ValueError("body")
    except ValueError:
        return Target, failure_events
    assert False


assert failed_definition("evaluate") == ("previous", ["evaluate"])
assert failed_definition("base") == ("previous", ["evaluate", "base"])
assert failed_definition("body") == ("previous", ["evaluate", "base", "body"])
assert failed_definition("apply") == (
    "previous", ["evaluate", "base", "body", "apply"]
)


def failed_first_binding():
    try:
        @selected := None
        class Unbound:
            pass
    except TypeError:
        assert selected is None
        try:
            Unbound
        except UnboundLocalError:
            return True
    return False


assert failed_first_binding()
