def expect_name_error(fn):
    raised = False
    try:
        fn()
    except NameError:
        raised = True
    assert raised


class SuppressingManager:
    def __enter__(self):
        return 6

    def __exit__(self, typ, exc, tb):
        return True


class ReraisingManager:
    def __enter__(self):
        return 8

    def __exit__(self, typ, exc, tb):
        return False


def local_read_before_assignment():
    value
    value = 1


expect_name_error(local_read_before_assignment)


def local_deleted_before_read():
    value = 1
    del value
    return value


expect_name_error(local_deleted_before_read)


def initialized_accumulator_in_for_loop(n):
    acc = 0
    for i in range(n):
        acc += i
    return acc


assert initialized_accumulator_in_for_loop(3) == 3


def for_continue_then_break_defines_local():
    for i in range(3):
        if not i:
            continue
        value = i
        break
    else:
        value = 99
    return value


assert for_continue_then_break_defines_local() == 1


def for_continue_exhausts_into_else_after_delete():
    value = 1
    for i in range(1):
        del value
        continue
        value = 2
    else:
        value = 9
    return value


assert for_continue_exhausts_into_else_after_delete() == 9


def for_break_bypasses_else_with_body_assignment():
    for i in range(1):
        value = 4
        break
    else:
        value = 5
    return value


assert for_break_bypasses_else_with_body_assignment() == 4


def for_empty_without_else_leaves_inner_assignment_missing():
    for i in range(0):
        value = 1
    return value


expect_name_error(for_empty_without_else_leaves_inner_assignment_missing)


def for_break_after_delete_bypasses_else_assignment():
    value = 1
    for i in range(1):
        del value
        break
    else:
        value = 2
    return value


expect_name_error(for_break_after_delete_bypasses_else_assignment)


def for_if_delete_branch_makes_later_read_checked(flag):
    value = 1
    for i in range(1):
        if flag:
            del value
        else:
            value = 2
    return value


def for_if_delete_branch_true_wrapper():
    return for_if_delete_branch_makes_later_read_checked(True)


expect_name_error(for_if_delete_branch_true_wrapper)
assert for_if_delete_branch_makes_later_read_checked(False) == 2


def while_else_after_false_condition_defines_local():
    i = 0
    while i:
        value = 1
        break
    else:
        value = 7
    return value


assert while_else_after_false_condition_defines_local() == 7


def while_continue_then_break_preserves_reachable_assignment():
    i = 0
    while 1:
        if i:
            value = i
            break
        i = 1
        continue
        value = 99
    else:
        value = 100
    return value


assert while_continue_then_break_preserves_reachable_assignment() == 1


def while_continue_after_delete_rechecks_condition():
    value = 1
    while value:
        del value
        continue
        value = 1
    return 0


expect_name_error(while_continue_after_delete_rechecks_condition)


def nested_for_break_delete_bypasses_inner_else_and_outer_breaks():
    value = 1
    while 1:
        for i in range(1):
            del value
            break
        else:
            value = 2
        break
    return value


expect_name_error(nested_for_break_delete_bypasses_inner_else_and_outer_breaks)


def nested_for_continue_reaches_inner_else_before_outer_break():
    value = 1
    while value:
        for i in range(1):
            del value
            continue
            value = 3
        else:
            value = 8
        break
    return value


assert nested_for_continue_reaches_inner_else_before_outer_break() == 8


def loop_return_expression_before_unreachable_assignment():
    for i in range(1):
        return value
        value = 1
    return 0


expect_name_error(loop_return_expression_before_unreachable_assignment)


def try_handler_sees_assignment_before_raise():
    try:
        for i in range(1):
            value = 3
            raise ValueError
            del value
    except ValueError:
        return value
    return 0


assert try_handler_sees_assignment_before_raise() == 3


def try_handler_does_not_see_assignment_after_raise():
    try:
        raise ValueError
        value = 1
    except ValueError:
        return value


expect_name_error(try_handler_does_not_see_assignment_after_raise)


def try_handler_reads_assignment_later_in_protected_body():
    try:
        1 / 0
        value = 1
    except ZeroDivisionError:
        return value


expect_name_error(try_handler_reads_assignment_later_in_protected_body)


def try_handler_entry_after_loop_delete_and_later_raise():
    value = 1
    try:
        for i in range(1):
            del value
            break
        1 / 0
    except ZeroDivisionError:
        pass
    return value


expect_name_error(try_handler_entry_after_loop_delete_and_later_raise)


def try_except_else_defines_after_no_exception():
    try:
        value = 1
    except ValueError:
        value = 2
    else:
        value = 3
    finally:
        pass
    return value


assert try_except_else_defines_after_no_exception() == 3


def try_except_handler_defines_after_exception():
    try:
        raise ValueError
    except ValueError:
        value = 4
    else:
        value = 5
    finally:
        pass
    return value


assert try_except_handler_defines_after_exception() == 4


def finally_entry_after_else_delete():
    value = 1
    try:
        pass
    except ValueError:
        pass
    else:
        del value
    finally:
        return value


expect_name_error(finally_entry_after_else_delete)


def finally_entry_after_handler_delete():
    value = 1
    try:
        raise ValueError
    except ValueError:
        del value
    finally:
        return value


expect_name_error(finally_entry_after_handler_delete)


def finally_during_return_after_delete():
    value = 1
    try:
        del value
        return 5
    finally:
        value


expect_name_error(finally_during_return_after_delete)


def after_try_finally_protected_delete_is_conservative(flag):
    value = 1
    try:
        if flag:
            del value
    finally:
        pass
    return value


def after_try_finally_true_wrapper():
    return after_try_finally_protected_delete_is_conservative(True)


expect_name_error(after_try_finally_true_wrapper)
assert after_try_finally_protected_delete_is_conservative(False) == 1


def return_before_unreachable_delete_through_finally():
    value = 1
    try:
        return value
        del value
    finally:
        pass


assert return_before_unreachable_delete_through_finally() == 1


def break_through_finally_after_delete():
    value = 1
    while 1:
        try:
            del value
            break
        finally:
            pass
    return value


expect_name_error(break_through_finally_after_delete)


def continue_through_finally_after_delete():
    value = 1
    for i in range(2):
        try:
            del value
            continue
        finally:
            pass
    return value


expect_name_error(continue_through_finally_after_delete)


def except_as_cleanup_deletes_handler_binding():
    try:
        1 / 0
    except ZeroDivisionError as exc:
        pass
    return exc


expect_name_error(except_as_cleanup_deletes_handler_binding)


def except_as_cleanup_deletes_preexisting_binding():
    exc = 123
    try:
        1 / 0
    except ZeroDivisionError as exc:
        pass
    return exc


expect_name_error(except_as_cleanup_deletes_preexisting_binding)


def with_as_target_defines_local():
    with ReraisingManager() as value:
        pass
    return value


assert with_as_target_defines_local() == 8


def with_body_delete_without_exception():
    value = 1
    with ReraisingManager():
        del value
    return value


expect_name_error(with_body_delete_without_exception)


def with_return_after_delete():
    value = 1
    with ReraisingManager():
        del value
        return value


expect_name_error(with_return_after_delete)


def with_suppressed_exception_continues_after_delete():
    value = 1
    with SuppressingManager():
        del value
        raise ValueError
        value = 2
    value = 7
    return value


assert with_suppressed_exception_continues_after_delete() == 7


def with_suppressed_exception_leaves_deleted_local_missing():
    value = 1
    with SuppressingManager():
        del value
        raise ValueError
        value = 2
    return value


expect_name_error(with_suppressed_exception_leaves_deleted_local_missing)


def with_suppressed_division_exception_leaves_deleted_local_missing():
    value = 1
    with SuppressingManager():
        del value
        1 / 0
        value = 2
    return value


expect_name_error(with_suppressed_division_exception_leaves_deleted_local_missing)


def with_nested_try_except_delete():
    value = 1
    with ReraisingManager():
        try:
            del value
            raise ValueError
            value = 1
        except ValueError:
            pass
    return value


expect_name_error(with_nested_try_except_delete)


def try_finally_inside_loop_continue_then_else_repairs_local():
    value = 1
    for i in range(1):
        try:
            del value
            continue
        finally:
            pass
    else:
        value = 10
    return value


assert try_finally_inside_loop_continue_then_else_repairs_local() == 10


def try_except_inside_loop_break_after_delete():
    value = 1
    for i in range(1):
        try:
            del value
            raise ValueError
        except ValueError:
            break
        value = 2
    else:
        value = 3
    return value


expect_name_error(try_except_inside_loop_break_after_delete)
