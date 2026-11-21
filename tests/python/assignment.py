a = 4
assert a + 3 == 7

a += 7
assert a == 11

value: int
assert 1 == 1

value: int = 7
assert value == 7


def annotate_parameter(value):
    value: int
    return value


def bare_parenthesized_annotation():
    (missing_name): int


def assign_annotated_global():
    (annotation_global): int
    global annotation_global
    (annotation_global): int = 9


assert annotate_parameter(5) == 5
assert bare_parenthesized_annotation() is None
assign_annotated_global()
assert annotation_global == 9

(module_annotation_name): UndefinedAnnotation
global module_annotation_name
(module_annotation_name): UndefinedAnnotation = 10
assert module_annotation_name == 10


class ParenthesizedAnnotation:
    (class_annotation_name): UndefinedAnnotation
    global class_annotation_name
    (class_annotation_name): UndefinedAnnotation = 11


assert class_annotation_name == 11


marker = 0

class AnnotationReceiver:
    pass

def side():
    global marker
    marker = 1
    return AnnotationReceiver()

side().missing: int
assert marker == 1

marker = 0

def side_list():
    global marker
    marker = 1
    return []

def key():
    global marker
    marker = marker + 2
    return 0

side_list()[key()]: int
assert marker == 3


# Attribute assignment evaluates its right-hand side before resolving the target.
class AssignmentBox:
    pass


assignment_a = AssignmentBox()
assignment_b = AssignmentBox()
assignment_a.value = 1
assignment_b.value = 2
assignment_current = assignment_a


def assignment_target():
    return assignment_current


def assignment_rhs():
    global assignment_current
    assignment_current = assignment_b
    return 7


assignment_target().value = assignment_rhs()
assert assignment_a.value == 1
assert assignment_b.value == 7

assignment_a.value = 1
assignment_b.value = 2
assignment_current = assignment_a
assignment_target().value: int = assignment_rhs()
assert assignment_a.value == 1
assert assignment_b.value == 7
