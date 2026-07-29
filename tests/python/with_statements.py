# with statements call enter and exit, support suppression, and unwind in reverse order.
log = 0


class Manager:
    def __enter__(self):
        return 4

    def __exit__(self, typ, exc, tb):
        global log
        log += 10
        return False


with Manager() as value:
    log = value + 1
assert log == 15

suppressed_value_error = False


class SuppressingManager:
    def __enter__(self):
        return self

    def __exit__(self, typ, exc, tb):
        global suppressed_value_error
        suppressed_value_error = typ is ValueError
        return True


with SuppressingManager():
    raise ValueError
assert suppressed_value_error

suppressed_target_error = False
values = []


class TargetSuppressingManager:
    def __enter__(self):
        return 7

    def __exit__(self, typ, exc, tb):
        global suppressed_target_error
        suppressed_target_error = typ is IndexError
        return True


with TargetSuppressingManager() as values[0]:
    suppressed_target_error = 99
assert suppressed_target_error is True

return_log = 0


class ReturnManager:
    def __enter__(self):
        return self

    def __exit__(self, typ, exc, tb):
        global return_log
        return_log = 10
        return False


def return_from_with():
    with ReturnManager():
        return 7
    return 99


assert return_from_with() + return_log == 17

exit_order = 0


class First:
    def __enter__(self):
        global exit_order
        exit_order = exit_order * 10 + 1
        return self

    def __exit__(self, typ, exc, tb):
        global exit_order
        exit_order = exit_order * 10 + 2
        return False


class Second:
    def __enter__(self):
        global exit_order
        exit_order = exit_order * 10 + 3
        return self

    def __exit__(self, typ, exc, tb):
        global exit_order
        exit_order = exit_order * 10 + 4
        return False


with First(), Second():
    exit_order = exit_order * 10 + 5
assert exit_order == 13542
