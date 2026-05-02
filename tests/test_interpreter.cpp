#include "attr.h"
#include "class_object.h"
#include "codegen.h"
#include "compilation_unit.h"
#include "dict.h"
#include "function.h"
#include "instance.h"
#include "interpreter.h"
#include "list.h"
#include "native_function.h"
#include "parser.h"
#include "range_iterator.h"
#include "scope.h"
#include "shape.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "token_print.h"
#include "tokenizer.h"
#include "tuple.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

static constexpr int64_t kMinSmi = -288230376151711744LL;

static void expect_runtime_error(const wchar_t *source,
                                 const char *expected_message)
{
    try
    {
        (void)test::FileRunner(source);
        FAIL() << "Expected std::runtime_error with message: "
               << expected_message;
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ(expected_message, err.what());
    }
}

static void expect_range_iterator(Value actual, int64_t expected_current,
                                  int64_t expected_stop, int64_t expected_step)
{
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::RangeIterator,
              actual.get_ptr<Object>()->native_layout_id());

    RangeIterator *iterator = actual.get_ptr<RangeIterator>();
    EXPECT_EQ(Value::from_smi(expected_current), iterator->current);
    EXPECT_EQ(Value::from_smi(expected_stop), iterator->stop);
    EXPECT_EQ(Value::from_smi(expected_step), iterator->step);
}

static int64_t g_next_counter = 0;

static Value native_next_counter() { return Value::from_smi(g_next_counter++); }

static Value native_zero() { return Value::from_smi(17); }

static Value native_increment(Value value)
{
    if(!value.is_smi())
    {
        throw std::runtime_error("native_increment expected a smi");
    }
    return Value::from_smi(value.get_smi() + 1);
}

static Value native_add(Value left, Value right)
{
    if(!left.is_smi() || !right.is_smi())
    {
        throw std::runtime_error("native_add expected smi arguments");
    }
    return Value::from_smi(left.get_smi() + right.get_smi());
}

static void bind_global(test::VmTestContext &test_context,
                        CodeObject *code_object, const wchar_t *name,
                        Value value)
{
    TValue<String> name_value(
        test_context.vm().get_or_create_interned_string_value(name));
    code_object->module_scope.extract()->set_by_name(name_value, value);
}

static Value make_test_function(test::VmTestContext &test_context,
                                const wchar_t *name, const wchar_t *source)
{
    CodeObject *code_object = test_context.compile_file(source);
    (void)test_context.thread()->run(code_object);

    TValue<String> name_value(
        test_context.vm().get_or_create_interned_string_value(name));
    Value function_value =
        code_object->module_scope.extract()->get_by_name(name_value);
    assert(function_value.is_ptr());
    assert(function_value.get_ptr<Object>()->native_layout_id() ==
           NativeLayoutId::Function);
    return function_value;
}

TEST(Interpreter, simple)
{
    Value expected = Value::from_smi(15);
    test::FileRunner file_runner(L"1 + 2  *  (4 + 3)");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple2)
{
    Value expected = Value::from_smi(19);
    test::FileRunner file_runner(L"(1 << 4) + 3");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple3)
{
    Value expected = Value::False();
    test::FileRunner file_runner(L"not True");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple4)
{
    Value expected = Value::from_smi(-13);
    test::FileRunner file_runner(L"1 - 2  *  (4 + 3)");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, assignment1)
{
    Value expected = Value::from_smi(7);
    test::FileRunner file_runner(L"a = 4\n"
                                 "a + 3");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, assignment2)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"a = 4\n"
                                 "a += 7\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while1)
{
    Value expected = Value::from_smi(4950);
    test::FileRunner file_runner(L"b = 0\n"
                                 "a = 100\n"
                                 "while a:\n"
                                 "    a -= 1\n"
                                 "    b += a\n"
                                 "b\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, if_elif_branch)
{
    Value expected = Value::from_smi(2);
    test::FileRunner file_runner(L"a = False\n"
                                 "b = True\n"
                                 "if a:\n"
                                 "    1\n"
                                 "elif b:\n"
                                 "    2\n"
                                 "else:\n"
                                 "    3\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, if_else_branch)
{
    Value expected = Value::from_smi(3);
    test::FileRunner file_runner(L"a = False\n"
                                 "b = False\n"
                                 "if a:\n"
                                 "    1\n"
                                 "elif b:\n"
                                 "    2\n"
                                 "else:\n"
                                 "    3\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while_else_runs_after_normal_exit)
{
    Value expected = Value::from_smi(7);
    test::FileRunner file_runner(L"a = 2\n"
                                 "b = 0\n"
                                 "while a:\n"
                                 "    a -= 1\n"
                                 "else:\n"
                                 "    b = 7\n"
                                 "b\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while_else_skipped_after_break)
{
    Value expected = Value::from_smi(0);
    test::FileRunner file_runner(L"a = 2\n"
                                 "b = 0\n"
                                 "while a:\n"
                                 "    break\n"
                                 "else:\n"
                                 "    b = 7\n"
                                 "b\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_multiple_parameters)
{
    Value expected = Value::from_smi(6);
    test::FileRunner file_runner(L"def add3(a, b, c):\n"
                                 "    return a + b + c\n"
                                 "add3(1, 2, 3)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_even_argument_call_preserves_frame_alignment)
{
    Value expected = Value::from_smi(10);
    test::FileRunner file_runner(L"def add4(a, b, c, d):\n"
                                 L"    return a + b + c + d\n"
                                 L"add4(1, 2, 3, 4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_default_parameters)
{
    Value expected = Value::from_smi(220);
    test::FileRunner file_runner(L"def add(a, b=10, c=100):\n"
                                 "    return a + b + c\n"
                                 "add(1) + add(1, 2) + add(1, 2, 3)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_defaults_evaluate_at_definition_time)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"x = 10\n"
                                 "def f(a=x + 1):\n"
                                 "    return a\n"
                                 "x = 20\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, method_defaults_evaluate_in_class_scope)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"class Cls:\n"
                                 "    x = 10\n"
                                 "    def method(self, a=x + 1):\n"
                                 "        return a\n"
                                 "Cls().method()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_wrong_arity)
{
    expect_runtime_error(L"def f(a):\n"
                         "    return a\n"
                         "f()\n",
                         "TypeError: wrong number of arguments");
    expect_runtime_error(L"def f(a):\n"
                         "    return a\n"
                         "f(1, 2)\n",
                         "TypeError: wrong number of arguments");
}

TEST(Interpreter, function_varargs_collect_extra_arguments)
{
    Value expected = Value::from_smi(10);
    test::FileRunner file_runner(L"def f(a, *args):\n"
                                 "    return a + args[0] + args[1]\n"
                                 "f(1, 4, 5)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_varargs_collect_empty_tuple)
{
    test::FileRunner file_runner(L"def f(*args):\n"
                                 "    return args\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_TRUE(TValue<Tuple>(actual).extract()->empty());
}

TEST(Interpreter, function_defaults_and_varargs)
{
    Value expected = Value::from_smi(65);
    test::FileRunner file_runner(L"def f(a, b=10, *args):\n"
                                 "    return a + b + args[0] + args[1]\n"
                                 "f(1, 20, 21, 23)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_defaults_and_empty_varargs)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"def f(a, b=10, *args):\n"
                                 "    return a + b\n"
                                 "f(1)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, method_varargs_collect_extra_arguments)
{
    Value expected = Value::from_smi(9);
    test::FileRunner file_runner(L"class Cls:\n"
                                 "    def method(self, *args):\n"
                                 "        return args[0] + args[1]\n"
                                 "Cls().method(4, 5)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_varargs_still_requires_positional_arguments)
{
    expect_runtime_error(L"def f(a, *args):\n"
                         "    return a\n"
                         "f()\n",
                         "TypeError: wrong number of arguments");
}

TEST(Interpreter, calls_and_parameters_accept_trailing_comma)
{
    Value expected = Value::from_smi(6);
    test::FileRunner file_runner(L"def add3(a, b, c,):\n"
                                 "    return a + b + c\n"
                                 "add3(1, 2, 3,)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_implicit_return_none)
{
    Value expected = Value::None();
    test::FileRunner file_runner(L"def f():\n"
                                 "    a = 1\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, class_definition_binds_class_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"Cls\n");

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Cls",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, class_body_assignment_becomes_class_member)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    value = 7\n");
    (void)test_context.thread()->run(code_obj);

    Value cls_value = code_obj->module_scope.extract()->get_by_name(cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    ClassObject *cls = cls_value.get_ptr<ClassObject>();
    EXPECT_EQ(Value::from_smi(7), load_attr(Value::from_oop(cls), value_name));

    Shape *shape = cls->get_shape();
    EXPECT_TRUE(shape->has_flag(ShapeFlag::IsClassObject));
    StorageLocation value_location =
        shape->resolve_present_property(value_name);
    ASSERT_TRUE(value_location.is_found());
    EXPECT_EQ(StorageKind::Inline, value_location.kind);
    EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotCount),
              value_location.physical_idx);
    EXPECT_EQ(Value::from_smi(7), cls->read_storage_location(value_location));
    ASSERT_EQ(ClassObject::kClassMetadataSlotCount + 1, shape->present_count());
    EXPECT_STREQ(L"value",
                 shape->get_property_name(ClassObject::kClassMetadataSlotCount)
                     .extract()
                     ->data);
}

TEST(Interpreter, class_body_attributes_preserve_shape_insertion_order)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> cls_name(
        test_context.vm().get_or_create_interned_string_value(L"Cls"));
    TValue<String> first_name(
        test_context.vm().get_or_create_interned_string_value(L"first"));
    TValue<String> second_name(
        test_context.vm().get_or_create_interned_string_value(L"second"));
    TValue<String> third_name(
        test_context.vm().get_or_create_interned_string_value(L"third"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    first = 1\n"
                                                     L"    second = 2\n"
                                                     L"    third = 3\n");
    (void)test_context.thread()->run(code_obj);

    Value cls_value = code_obj->module_scope.extract()->get_by_name(cls_name);
    ASSERT_TRUE(cls_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              cls_value.get_ptr<Object>()->native_layout_id());
    ClassObject *cls = cls_value.get_ptr<ClassObject>();

    TValue<String> names[] = {first_name, second_name, third_name};
    for(uint32_t idx = 0; idx < 3; ++idx)
    {
        EXPECT_STREQ(string_as_wchar_t(names[idx]),
                     string_as_wchar_t(cls->get_shape()->get_property_name(
                         ClassObject::kClassMetadataSlotCount + idx)));

        StorageLocation location =
            cls->get_shape()->resolve_present_property(names[idx]);
        ASSERT_TRUE(location.is_found());
        EXPECT_EQ(StorageKind::Inline, location.kind);
        EXPECT_EQ(int32_t(ClassObject::kClassPredefinedSlotCount + idx),
                  location.physical_idx);
        EXPECT_EQ(Value::from_smi(idx + 1),
                  cls->read_storage_location(location));
    }
}

TEST(Interpreter, class_body_readonly_metadata_store_is_rejected)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    __name__ = 1\n",
                         "TypeError: cannot set read-only class attribute");
}

TEST(Interpreter, set_name_notification_is_explicitly_unsupported)
{
    expect_runtime_error(L"class Descriptor:\n"
                         L"    def __set_name__(self, owner, name):\n"
                         L"        self.owner = owner\n"
                         L"class Owner:\n"
                         L"    field = Descriptor()\n",
                         "TypeError: __set_name__ notifications are not "
                         "implemented yet");
}

TEST(Interpreter, class_call_allocates_instance_with_initial_class_slot)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.__class__ is Cls\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::True(), actual);
}

TEST(Interpreter, class_body_can_read_earlier_class_binding)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    x = 1\n"
                                 L"    y = x + 2\n"
                                 L"Cls.y\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, class_definition_inside_function_preserves_caller_frame)
{
    test::FileRunner file_runner(L"def outer(seed):\n"
                                 L"    a = seed + 1\n"
                                 L"    b = seed + 2\n"
                                 L"    c = (a + b) * (seed + 3)\n"
                                 L"    class Cls:\n"
                                 L"        x = 11\n"
                                 L"        y = x + 13\n"
                                 L"    d = (a + b) * (c + Cls.y)\n"
                                 L"    return d + a + b + c\n"
                                 L"outer(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(1199), actual);
}

TEST(Interpreter, class_call_allocates_instance)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"Cls()\n");

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              actual.get_ptr<Object>()->native_layout_id());
    ASSERT_TRUE(actual.get_ptr<Instance>()->get_class().as_value().is_ptr());
    EXPECT_EQ(NativeLayoutId::ClassObject, actual.get_ptr<Instance>()
                                               ->get_class()
                                               .as_value()
                                               .get_ptr<Object>()
                                               ->native_layout_id());
}

TEST(Interpreter, class_constructor_calls_init_and_forwards_arguments)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def __init__(self, x, y):\n"
                                 L"        self.value = x * 10 + y\n"
                                 L"obj = Cls(4, 7)\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(47), actual);
}

TEST(Interpreter, class_constructor_uses_init_defaults)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def __init__(self, x, y=3):\n"
                                 L"        self.value = x * 10 + y\n"
                                 L"obj = Cls(4)\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(43), actual);
}

TEST(Interpreter, class_constructor_forwards_varargs_tuple)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def __init__(self, x, *rest):\n"
                                 L"        self.value = x + rest[0] + rest[1]\n"
                                 L"obj = Cls(1, 20, 300)\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(321), actual);
}

TEST(Interpreter, class_constructor_rejects_non_none_init_return)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    def __init__(self):\n"
                         L"        return 1\n"
                         L"Cls()\n",
                         "TypeError: __init__ should return None, not a value");
}

TEST(Interpreter, class_constructor_rebuilds_after_init_change)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def __init__(self):\n"
                                 L"        self.value = 1\n"
                                 L"def replacement(self):\n"
                                 L"    self.value = 2\n"
                                 L"first = Cls()\n"
                                 L"Cls.__init__ = replacement\n"
                                 L"second = Cls()\n"
                                 L"first.value * 10 + second.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, class_method_call_works_from_source)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(self, x):\n"
                                 L"        return self.value + x\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 3\n"
                                 L"obj.method(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, string_literal_value)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"abc\"\n");

    EXPECT_STREQ(L"abc", string_as_wchar_t(TValue<String>(actual)));
}

TEST(Interpreter, string_dunder_add_calls_native_function)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"\"ab\".__add__(\"cd\")\n");

    ASSERT_TRUE(can_convert_to<String>(actual));
    EXPECT_STREQ(L"abcd", string_as_wchar_t(TValue<String>(actual)));
}

TEST(Interpreter, string_dunder_add_wrong_type_reports_unimplemented)
{
    expect_runtime_error(L"\"ab\".__add__(3)\n", "UnimplementedError");
}

TEST(Interpreter, list_literal_returns_list_object)
{
    test::FileRunner file_runner(L"[1, 2, 4]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), list->item_unchecked(2));
}

TEST(Interpreter, empty_list_literal_returns_empty_list)
{
    test::FileRunner file_runner(L"[]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    EXPECT_TRUE(list->empty());
    EXPECT_EQ(0u, list->size());
}

TEST(Interpreter, list_literal_evaluates_elements_left_to_right)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(
        L"[next_counter(), next_counter(), next_counter()]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::List,
              actual.get_ptr<Object>()->native_layout_id());
    List *list = actual.get_ptr<List>();
    ASSERT_EQ(3u, list->size());
    EXPECT_EQ(Value::from_smi(0), list->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(1), list->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(2), list->item_unchecked(2));
}

TEST(Interpreter, tuple_literal_returns_tuple_object)
{
    test::FileRunner file_runner(L"1, 2, 4\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), tuple->item_unchecked(2));
}

TEST(Interpreter, parenthesized_tuple_literal_returns_tuple_object)
{
    test::FileRunner file_runner(L"(1, 2, 4)\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(4), tuple->item_unchecked(2));
}

TEST(Interpreter, empty_tuple_literal_returns_empty_tuple_object)
{
    test::FileRunner file_runner(L"()\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    EXPECT_EQ(0u, tuple->size());
}

TEST(Interpreter, tuple_literal_evaluates_elements_left_to_right)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(
        L"next_counter(), next_counter(), next_counter()\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    Tuple *tuple = actual.get_ptr<Tuple>();
    ASSERT_EQ(3u, tuple->size());
    EXPECT_EQ(Value::from_smi(0), tuple->item_unchecked(0));
    EXPECT_EQ(Value::from_smi(1), tuple->item_unchecked(1));
    EXPECT_EQ(Value::from_smi(2), tuple->item_unchecked(2));
}

TEST(Interpreter, subscript_load_reads_list_item)
{
    test::FileRunner file_runner(L"xs = [4, 7, 9]\n"
                                 L"xs[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, subscript_load_reads_tuple_item)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[0]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Cls",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, subscript_load_reads_tuple_item_with_negative_index)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[-1]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"object",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, dict_literal_returns_dict_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    Value actual = test_context.run_file(L"key = \"alpha\"\n"
                                         L"{key: 7, \"beta\": 9}\n");

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Dict,
              actual.get_ptr<Object>()->native_layout_id());
    Dict *dict = actual.get_ptr<Dict>();
    EXPECT_EQ(2u, dict->size());

    Value alpha =
        test_context.vm().get_or_create_interned_string_value(L"alpha");
    Value beta = test_context.vm().get_or_create_interned_string_value(L"beta");
    EXPECT_EQ(Value::from_smi(7), dict->get_item(alpha));
    EXPECT_EQ(Value::from_smi(9), dict->get_item(beta));
}

TEST(Interpreter, subscript_load_reads_dict_item)
{
    test::FileRunner file_runner(L"key = \"alpha\"\n"
                                 L"xs = {key: 4, \"beta\": 7}\n"
                                 L"xs[key]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(4), actual);
}

TEST(Interpreter, subscript_store_writes_dict_item)
{
    test::FileRunner file_runner(L"xs = {\"alpha\": 4, \"beta\": 7}\n"
                                 L"xs[\"beta\"] = 11\n"
                                 L"xs[\"beta\"]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, subscript_augmented_assignment_updates_dict_item)
{
    test::FileRunner file_runner(L"xs = {\"alpha\": 4, \"beta\": 7}\n"
                                 L"xs[\"beta\"] += 5\n"
                                 L"xs[\"beta\"]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter,
     dict_subscript_augmented_assignment_evaluates_receiver_and_key_once)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"xs = {\"alpha\": 10, \"beta\": 20}\n"
                                  L"def get_dict():\n"
                                  L"    return xs\n"
                                  L"def next_key():\n"
                                  L"    next_counter()\n"
                                  L"    return \"alpha\"\n"
                                  L"get_dict()[next_key()] += 7\n"
                                  L"xs[\"alpha\"]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_store_writes_list_item)
{
    test::FileRunner file_runner(L"xs = [4, 7, 9]\n"
                                 L"xs[1] = 11\n"
                                 L"xs[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, subscript_store_rejects_tuple_item_assignment)
{
    expect_runtime_error(
        L"class Cls:\n"
        L"    pass\n"
        L"Cls.__mro__[0] = 1\n",
        "TypeError: 'tuple' object does not support item assignment");
}

TEST(Interpreter, subscript_augmented_assignment_updates_list_item)
{
    test::FileRunner file_runner(L"xs = [4, 7, 9]\n"
                                 L"xs[1] += 5\n"
                                 L"xs[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter,
     subscript_augmented_assignment_evaluates_receiver_and_index_once)
{
    g_next_counter = 0;

    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj =
        test_context.compile_file(L"xs = [10, 20, 30]\n"
                                  L"def get_list():\n"
                                  L"    return xs\n"
                                  L"get_list()[next_counter()] += 7\n"
                                  L"xs[0]\n");

    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"next_counter");
    Value next_counter =
        make_native_function(&test_context.vm(), native_next_counter);
    code_obj->module_scope.extract()->set_by_name(name, next_counter);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
    EXPECT_EQ(1, g_next_counter);
}

TEST(Interpreter, subscript_load_rejects_non_integer_list_index)
{
    expect_runtime_error(L"xs = [1, 2, 3]\n"
                         L"xs[False]\n",
                         "TypeError: list indices must be integers");
}

TEST(Interpreter, subscript_load_rejects_non_integer_tuple_index)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    pass\n"
                         L"Cls.__mro__[False]\n",
                         "TypeError: tuple indices must be integers");
}

TEST(Interpreter, subscript_load_rejects_out_of_range_tuple_index)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    pass\n"
                         L"Cls.__mro__[2]\n",
                         "IndexError: tuple index out of range");
}

TEST(Interpreter, subscript_load_rejects_non_subscriptable_receiver)
{
    expect_runtime_error(L"1[0]\n", "TypeError: object is not subscriptable");
}

TEST(Interpreter, attribute_load_through_list_subscript)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"lst = [obj]\n"
                                 L"lst[0].value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, attribute_store_through_list_subscript)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"lst = [obj]\n"
                                 L"lst[0].value = 11\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, attribute_augmented_assignment_through_list_subscript)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"lst = [obj]\n"
                                 L"lst[0].value += 5\n"
                                 L"obj.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, subscript_load_through_object_attribute)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.lst = [4, 7, 9]\n"
                                 L"obj.lst[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, subscript_store_through_object_attribute)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.lst = [4, 7, 9]\n"
                                 L"obj.lst[1] = 11\n"
                                 L"obj.lst[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(11), actual);
}

TEST(Interpreter, subscript_augmented_assignment_through_object_attribute)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"obj = Cls()\n"
                                 L"obj.lst = [4, 7, 9]\n"
                                 L"obj.lst[1] += 5\n"
                                 L"obj.lst[1]\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, attribute_load_and_store_syntax)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> obj_name(
        test_context.vm().get_or_create_interned_string_value(L"obj"));
    TValue<String> attr_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"obj = Cls()\n"
                                                     L"obj.value = 7\n"
                                                     L"obj.value\n");
    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(7), actual);
    Scope *module_scope = code_obj->module_scope.extract();
    Value obj_value = module_scope->get_by_name(obj_name);
    ASSERT_TRUE(obj_value.is_ptr());
    ASSERT_EQ(NativeLayoutId::Instance,
              obj_value.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(Value::from_smi(7),
              obj_value.get_ptr<Instance>()->get_own_property(attr_name));
}

TEST(Interpreter, store_attr_caches_instance_add_transition)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> function_name(
        test_context.vm().get_or_create_interned_string_value(L"make"));
    CodeObject *code_obj = test_context.compile_file(L"class Cls:\n"
                                                     L"    pass\n"
                                                     L"def make(value):\n"
                                                     L"    obj = Cls()\n"
                                                     L"    obj.value = value\n"
                                                     L"    return obj.value\n"
                                                     L"make(1)\n"
                                                     L"make(2)\n");

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(2), actual);

    Value function_value =
        code_obj->module_scope.extract()->get_by_name(function_name);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    CodeObject *function_code =
        assume_convert_to<Function>(function_value)->code_object.extract();
    ASSERT_EQ(1u, function_code->attribute_write_caches.size());
    const AttributeWriteInlineCache &cache =
        function_code->attribute_write_caches[0];
    EXPECT_EQ(AttributeWritePlanKind::AddOwnProperty, cache.plan.kind);
    ASSERT_NE(nullptr, cache.receiver_shape);
    ASSERT_NE(nullptr, cache.plan.add_next_shape);
    ASSERT_TRUE(cache.plan.storage_location.is_found());
    ASSERT_NE(nullptr, cache.plan.lookup_validity_cell);
    EXPECT_TRUE(cache.plan.lookup_validity_cell->is_valid());
}

TEST(Interpreter, cached_class_attribute_read_observes_class_write)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    value = 1\n"
                                 L"def get(obj):\n"
                                 L"    return obj.value\n"
                                 L"obj = Cls()\n"
                                 L"first = get(obj)\n"
                                 L"Cls.value = 2\n"
                                 L"get(obj)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, cached_class_chain_attribute_read_observes_mro_mutations)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class());
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    ASSERT_TRUE(base->set_own_property(value_name, Value::from_smi(1)));

    CodeObject *read_code = test_context.compile_file(L"obj.value\n");
    bind_global(test_context, read_code, L"obj", Value::from_oop(obj));

    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(read_code));

    EXPECT_TRUE(base->set_own_property(value_name, Value::from_smi(2)));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(read_code));

    EXPECT_TRUE(mid->set_own_property(value_name, Value::from_smi(3)));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(read_code));

    EXPECT_TRUE(leaf->set_own_property(value_name, Value::from_smi(4)));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));

    EXPECT_TRUE(obj->set_own_property(value_name, Value::from_smi(5)));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    EXPECT_TRUE(base->set_own_property(value_name, Value::from_smi(6)));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    EXPECT_TRUE(obj->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));

    EXPECT_TRUE(leaf->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(read_code));

    EXPECT_TRUE(mid->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(6), test_context.thread()->run(read_code));
}

TEST(Interpreter, cached_class_chain_attribute_read_observes_secondary_base)
{
    test::FileRunner file_runner(L"class Left:\n"
                                 L"    pass\n"
                                 L"class Right:\n"
                                 L"    value = 1\n"
                                 L"class Derived(Left, Right):\n"
                                 L"    pass\n"
                                 L"def get(obj):\n"
                                 L"    return obj.value\n"
                                 L"obj = Derived()\n"
                                 L"first = get(obj)\n"
                                 L"Right.value = 2\n"
                                 L"get(obj)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, cached_direct_method_call_observes_mro_mutations)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> method_name(
        test_context.vm().get_or_create_interned_string_value(L"method"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class());
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    Value base_method_1 = make_test_function(test_context, L"base_method_1",
                                             L"def base_method_1(self):\n"
                                             L"    return 1\n");
    Value base_method_2 = make_test_function(test_context, L"base_method_2",
                                             L"def base_method_2(self):\n"
                                             L"    return 2\n");
    Value mid_method = make_test_function(test_context, L"mid_method",
                                          L"def mid_method(self):\n"
                                          L"    return 3\n");
    Value leaf_method = make_test_function(test_context, L"leaf_method",
                                           L"def leaf_method(self):\n"
                                           L"    return 4\n");
    Value own_method = make_test_function(test_context, L"own_method",
                                          L"def own_method():\n"
                                          L"    return 5\n");

    ASSERT_TRUE(base->set_own_property(method_name, base_method_1));

    CodeObject *call_code = test_context.compile_file(L"obj.method()\n");
    bind_global(test_context, call_code, L"obj", Value::from_oop(obj));

    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(call_code));

    EXPECT_TRUE(base->set_own_property(method_name, base_method_2));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(call_code));

    EXPECT_TRUE(mid->set_own_property(method_name, mid_method));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(call_code));

    EXPECT_TRUE(leaf->set_own_property(method_name, leaf_method));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(call_code));

    EXPECT_TRUE(obj->set_own_property(method_name, own_method));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(call_code));

    EXPECT_TRUE(base->set_own_property(method_name, base_method_1));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(call_code));

    EXPECT_TRUE(obj->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(call_code));

    EXPECT_TRUE(leaf->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(call_code));

    EXPECT_TRUE(mid->delete_own_property(method_name));
    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(call_code));
}

TEST(Interpreter, cached_attribute_stores_invalidate_class_chain_reads)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    TValue<String> base_name(
        test_context.vm().get_or_create_interned_string_value(L"Base"));
    TValue<String> mid_name(
        test_context.vm().get_or_create_interned_string_value(L"Mid"));
    TValue<String> leaf_name(
        test_context.vm().get_or_create_interned_string_value(L"Leaf"));
    TValue<String> value_name(
        test_context.vm().get_or_create_interned_string_value(L"value"));

    ClassObject *base = test_context.thread()->make_internal_raw<ClassObject>(
        base_name, 4, test_context.vm().object_class());
    ClassObject *mid = test_context.thread()->make_internal_raw<ClassObject>(
        mid_name, 4, base);
    ClassObject *leaf = test_context.thread()->make_internal_raw<ClassObject>(
        leaf_name, 4, mid);
    Instance *obj = test_context.thread()->make_internal_raw<Instance>(leaf);

    ASSERT_TRUE(base->set_own_property(value_name, Value::from_smi(1)));

    CodeObject *read_code = test_context.compile_file(L"obj.value\n");
    bind_global(test_context, read_code, L"obj", Value::from_oop(obj));
    EXPECT_EQ(Value::from_smi(1), test_context.thread()->run(read_code));

    CodeObject *base_store_code =
        test_context.compile_file(L"Base.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, base_store_code, L"Base", Value::from_oop(base));
    bind_global(test_context, base_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, base_store_code, L"new_value",
                Value::from_smi(2));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(base_store_code));
    EXPECT_EQ(Value::from_smi(2), test_context.thread()->run(read_code));

    CodeObject *mid_store_code =
        test_context.compile_file(L"Mid.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, mid_store_code, L"Mid", Value::from_oop(mid));
    bind_global(test_context, mid_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, mid_store_code, L"new_value", Value::from_smi(3));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(mid_store_code));
    EXPECT_EQ(Value::from_smi(3), test_context.thread()->run(read_code));

    CodeObject *leaf_store_code =
        test_context.compile_file(L"Leaf.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, leaf_store_code, L"Leaf", Value::from_oop(leaf));
    bind_global(test_context, leaf_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, leaf_store_code, L"new_value",
                Value::from_smi(4));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(leaf_store_code));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));

    CodeObject *self_store_code =
        test_context.compile_file(L"obj.value = new_value\n"
                                  L"obj.value\n");
    bind_global(test_context, self_store_code, L"obj", Value::from_oop(obj));

    bind_global(test_context, self_store_code, L"new_value",
                Value::from_smi(5));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(self_store_code));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    bind_global(test_context, base_store_code, L"new_value",
                Value::from_smi(6));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(base_store_code));
    EXPECT_EQ(Value::from_smi(5), test_context.thread()->run(read_code));

    EXPECT_TRUE(obj->delete_own_property(value_name));
    EXPECT_EQ(Value::from_smi(4), test_context.thread()->run(read_code));
}

TEST(Interpreter, direct_method_call_inserts_self_for_class_functions)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(self, x):\n"
                                 L"        return self.value + x\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 3\n"
                                 L"obj.method(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter,
     direct_zero_arg_method_call_inserts_self_and_preserves_frame_alignment)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(self):\n"
                                 L"        return self.value\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 7\n"
                                 L"obj.method()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter,
     direct_method_call_with_odd_effective_args_preserves_frame_alignment)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(self, x, y):\n"
                                 L"        return self.value + x + y\n"
                                 L"obj = Cls()\n"
                                 L"obj.value = 3\n"
                                 L"obj.method(4, 5)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, direct_method_call_on_class_does_not_insert_self)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(x):\n"
                                 L"        return x + 3\n"
                                 L"Cls.method(4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, direct_zero_arg_class_function_call_preserves_frame_alignment)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method():\n"
                                 L"        return 7\n"
                                 L"Cls.method()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter,
     direct_class_function_call_with_even_args_preserves_frame_alignment)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    def method(x, y):\n"
                                 L"        return x + y\n"
                                 L"Cls.method(4, 5)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, class_definition_uses_explicit_base)
{
    test::FileRunner file_runner(L"class Base:\n"
                                 L"    value = 7\n"
                                 L"class Derived(Base):\n"
                                 L"    pass\n"
                                 L"Derived.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, class_definition_stores_explicit_base_tuple)
{
    test::FileRunner file_runner(L"class Base:\n"
                                 L"    pass\n"
                                 L"class Derived(Base):\n"
                                 L"    pass\n"
                                 L"Derived.__bases__[0]\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::ClassObject,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_STREQ(L"Base",
                 string_as_wchar_t(actual.get_ptr<ClassObject>()->get_name()));
}

TEST(Interpreter, class_definition_stores_implicit_object_base)
{
    test::FileRunner file_runner(L"class Cls:\n"
                                 L"    pass\n"
                                 L"Cls.__bases__[0] is object\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::True(), actual);
}

TEST(Interpreter, class_definition_stores_explicit_object_base)
{
    test::FileRunner file_runner(L"class Cls(object):\n"
                                 L"    pass\n"
                                 L"Cls.__mro__[1] is object\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::True(), actual);
}

TEST(Interpreter, class_definition_stores_multiple_base_tuple)
{
    test::FileRunner file_runner(L"class Left:\n"
                                 L"    marker = 1\n"
                                 L"class Right:\n"
                                 L"    marker = 2\n"
                                 L"class Derived(Left, Right):\n"
                                 L"    pass\n"
                                 L"Derived.__bases__[1].marker\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, class_definition_allows_object_after_derived_base)
{
    test::FileRunner file_runner(L"class Base:\n"
                                 L"    pass\n"
                                 L"class Derived(Base, object):\n"
                                 L"    pass\n"
                                 L"Derived.__mro__[1] is Base\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::True(), actual);
}

TEST(Interpreter, object_class_has_empty_bases_tuple)
{
    test::FileRunner file_runner(L"object.__bases__\n");
    Value actual = file_runner.return_value;

    ASSERT_TRUE(actual.is_ptr());
    ASSERT_EQ(NativeLayoutId::Tuple,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_TRUE(actual.get_ptr<Tuple>()->empty());
}

TEST(Interpreter, class_definition_rejects_non_class_base)
{
    expect_runtime_error(L"Base = 1\n"
                         L"class Derived(Base):\n"
                         L"    pass\n",
                         "TypeError: class bases must be class objects");
}

TEST(Interpreter, class_definition_rejects_duplicate_base)
{
    expect_runtime_error(L"class Base:\n"
                         L"    pass\n"
                         L"class Derived(Base, Base):\n"
                         L"    pass\n",
                         "TypeError: duplicate base class");
}

TEST(Interpreter, class_definition_uses_multiple_base_mro_order)
{
    test::FileRunner file_runner(L"class Left:\n"
                                 L"    value = 7\n"
                                 L"class Right:\n"
                                 L"    value = 8\n"
                                 L"class Derived(Left, Right):\n"
                                 L"    pass\n"
                                 L"Derived.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(7), actual);
}

TEST(Interpreter, class_definition_uses_c3_diamond_mro)
{
    test::FileRunner file_runner(L"class Top:\n"
                                 L"    value = 1\n"
                                 L"class Left(Top):\n"
                                 L"    pass\n"
                                 L"class Right(Top):\n"
                                 L"    value = 2\n"
                                 L"class Bottom(Left, Right):\n"
                                 L"    pass\n"
                                 L"Bottom.value\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(2), actual);
}

TEST(Interpreter, class_definition_exposes_c3_mro_tuple)
{
    test::FileRunner file_runner(L"class Top:\n"
                                 L"    marker = 1\n"
                                 L"class Left(Top):\n"
                                 L"    marker = 2\n"
                                 L"class Right(Top):\n"
                                 L"    marker = 3\n"
                                 L"class Bottom(Left, Right):\n"
                                 L"    pass\n"
                                 L"Bottom.__mro__[1].marker * 100 + "
                                 L"Bottom.__mro__[2].marker * 10 + "
                                 L"Bottom.__mro__[3].marker\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(231), actual);
}

TEST(Interpreter, class_definition_rejects_inconsistent_c3_mro)
{
    expect_runtime_error(L"class X:\n"
                         L"    pass\n"
                         L"class Y:\n"
                         L"    pass\n"
                         L"class A(X, Y):\n"
                         L"    pass\n"
                         L"class B(Y, X):\n"
                         L"    pass\n"
                         L"class C(A, B):\n"
                         L"    pass\n",
                         "TypeError: cannot create a consistent method "
                         "resolution order");
}

TEST(Interpreter, class_call_rejects_arguments)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    pass\n"
                         L"Cls(1)\n",
                         "TypeError: wrong number of arguments");
}

TEST(Interpreter, return_in_class_body_is_rejected)
{
    expect_runtime_error(L"class Cls:\n"
                         L"    return 1\n",
                         "SyntaxError: 'return' outside function");
}

TEST(Interpreter, return_in_module_body_is_rejected)
{
    expect_runtime_error(L"return\n", "SyntaxError: 'return' outside function");
}

TEST(Interpreter, function_local_shadows_global)
{
    Value expected = Value::from_smi(11);
    test::FileRunner file_runner(L"a = 10\n"
                                 "def f():\n"
                                 "    a = 1\n"
                                 "    return a\n"
                                 "f() + a\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_reads_global_when_not_shadowed)
{
    Value expected = Value::from_smi(10);
    test::FileRunner file_runner(L"a = 10\n"
                                 "def f():\n"
                                 "    return a\n"
                                 "f()\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, function_nested_control_flow)
{
    Value expected = Value::from_smi(2);
    test::FileRunner file_runner(L"def pick(n):\n"
                                 "    if n:\n"
                                 "        while n:\n"
                                 "            return 1\n"
                                 "    else:\n"
                                 "        return 2\n"
                                 "pick(0)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, recursive_fibonacci)
{
    Value expected = Value::from_smi(10946);
    test::FileRunner file_runner(L"def fib(n):\n"
                                 "    if n <= 2:\n"
                                 "        return n\n"
                                 "    return fib(n-2) + fib(n-1)\n"
                                 "\n"
                                 "fib(20)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, name_error)
{
    expect_runtime_error(L"missing_name\n", "NameError");
}

TEST(Interpreter, call_non_callable)
{
    expect_runtime_error(L"1()\n", "TypeError: object is not callable");
}

TEST(Interpreter, call_native_zero_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_zero()\n");

    bind_global(test_context, code_obj, L"native_zero",
                make_native_function(&test_context.vm(), native_zero));

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(17), actual);
}

TEST(Interpreter, call_native_one_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_increment(41)\n");

    bind_global(test_context, code_obj, L"native_increment",
                make_native_function(&test_context.vm(), native_increment));

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, call_native_two_arg_function)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *code_obj = test_context.compile_file(L"native_add(20, 22)\n");

    bind_global(test_context, code_obj, L"native_add",
                make_native_function(&test_context.vm(), native_add));

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, builtin_scope_lookup)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(L"range\n");

    Scope *module_scope = code_obj->module_scope.extract();
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"range");
    int32_t slot_idx = module_scope->lookup_slot_index_local(name);
    ASSERT_GE(slot_idx, 0);

    Value actual = test_context.thread()->run(code_obj);
    EXPECT_EQ(NativeLayoutId::Function,
              actual.get_ptr<Object>()->native_layout_id());
    EXPECT_EQ(actual, module_scope->get_by_slot_index_fastpath_only(slot_idx));
}

TEST(Interpreter, builtin_type_classes_are_vm_roots_and_builtins)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());

    struct ExpectedBuiltinType
    {
        NativeLayoutId native_layout_id;
        const wchar_t *name;
    };

    constexpr ExpectedBuiltinType expected_types[] = {
        {NativeLayoutId::ClassObject, L"type"},
        {NativeLayoutId::String, L"str"},
        {NativeLayoutId::List, L"list"},
        {NativeLayoutId::Tuple, L"tuple"},
        {NativeLayoutId::Dict, L"dict"},
        {NativeLayoutId::Function, L"function"},
        {NativeLayoutId::CodeObject, L"code"},
        {NativeLayoutId::RangeIterator, L"range_iterator"},
        {NativeLayoutId::Instance, L"object"},
    };

    Scope *builtins = test_context.vm().get_builtin_scope().extract();
    ClassObject *type_class = test_context.vm().type_class();
    ASSERT_NE(nullptr, type_class);

    for(const ExpectedBuiltinType &expected: expected_types)
    {
        ClassObject *cls = test_context.vm().class_for_native_layout(
            expected.native_layout_id);
        ASSERT_NE(nullptr, cls);
        EXPECT_EQ(type_class, cls->Object::get_class().extract());
        EXPECT_EQ(-1, cls->refcount);
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsClassObject));
        EXPECT_TRUE(cls->get_shape()->has_flag(ShapeFlag::IsImmutableType));
        EXPECT_EQ(Value::from_oop(type_class),
                  cls->get_own_property(
                      test_context.vm().get_or_create_interned_string_value(
                          L"__class__")));

        TValue<String> name =
            test_context.vm().get_or_create_interned_string_value(
                expected.name);
        EXPECT_EQ(name, cls->get_name());
        EXPECT_EQ(test_context.vm().str_class(),
                  name.extract()->Object::get_class().extract());
        EXPECT_EQ(test_context.vm().str_instance_root_shape(),
                  name.extract()
                      ->Object::get_class()
                      .extract()
                      ->get_instance_root_shape());

        TValue<String> dunder_bases_name =
            test_context.vm().get_or_create_interned_string_value(L"__bases__");
        TValue<String> dunder_mro_name =
            test_context.vm().get_or_create_interned_string_value(L"__mro__");
        Value bases_value = cls->get_own_property(dunder_bases_name);
        Value mro_value = cls->get_own_property(dunder_mro_name);
        ASSERT_TRUE(can_convert_to<Tuple>(bases_value));
        ASSERT_TRUE(can_convert_to<Tuple>(mro_value));
        EXPECT_EQ(test_context.vm().tuple_class(),
                  bases_value.get_ptr<Object>()->get_class().extract());
        EXPECT_EQ(test_context.vm().tuple_class(),
                  mro_value.get_ptr<Object>()->get_class().extract());
        Tuple *bases = bases_value.get_ptr<Tuple>();
        Tuple *mro = mro_value.get_ptr<Tuple>();
        if(expected.native_layout_id == NativeLayoutId::Instance)
        {
            EXPECT_EQ(0u, bases->size());
            ASSERT_EQ(1u, mro->size());
            EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
        }
        else
        {
            ASSERT_EQ(1u, bases->size());
            EXPECT_EQ(Value::from_oop(test_context.vm().object_class()),
                      bases->item_unchecked(0));
            ASSERT_EQ(2u, mro->size());
            EXPECT_EQ(Value::from_oop(cls), mro->item_unchecked(0));
            EXPECT_EQ(Value::from_oop(test_context.vm().object_class()),
                      mro->item_unchecked(1));
        }

        if(expected.native_layout_id == NativeLayoutId::CodeObject)
        {
            EXPECT_EQ(Value::not_present(), builtins->get_by_name(name));
        }
        else
        {
            EXPECT_EQ(Value::from_oop(cls), builtins->get_by_name(name));
        }
    }

    TValue<String> post_bootstrap_name =
        test_context.vm().get_or_create_interned_string_value(
            L"post_bootstrap_name");
    EXPECT_EQ(test_context.vm().str_class(),
              post_bootstrap_name.extract()->Object::get_class().extract());
    EXPECT_EQ(test_context.vm().str_instance_root_shape(),
              post_bootstrap_name.extract()
                  ->Object::get_class()
                  .extract()
                  ->get_instance_root_shape());

    EXPECT_EQ(Value::from_oop(type_class),
              type_class->get_own_property(
                  test_context.vm().get_or_create_interned_string_value(
                      L"__class__")));

    ClassObject *str_class = test_context.vm().str_class();
    TValue<String> dunder_str_name =
        test_context.vm().get_or_create_interned_string_value(L"__str__");
    TValue<String> dunder_add_name =
        test_context.vm().get_or_create_interned_string_value(L"__add__");
    Value str_method = str_class->get_own_property(dunder_str_name);
    Value add_method = str_class->get_own_property(dunder_add_name);
    ASSERT_TRUE(can_convert_to<Function>(str_method));
    ASSERT_TRUE(can_convert_to<Function>(add_method));
    EXPECT_EQ(-1, str_method.get_ptr<Object>()->refcount);
    EXPECT_EQ(-1, add_method.get_ptr<Object>()->refcount);
    EXPECT_FALSE(
        str_class->set_own_property(dunder_str_name, Value::from_smi(99)));
}

TEST(Interpreter, range_builtin_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(5)\n");

    expect_range_iterator(actual, 0, 5, 1);
}

TEST(Interpreter, range_builtin_two_arguments_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(2, 5)\n");

    expect_range_iterator(actual, 2, 5, 1);
}

TEST(Interpreter, range_builtin_three_arguments_returns_range_iterator)
{
    test::VmTestContext test_context;
    Value actual = test_context.run_file(L"range(2, 9, 3)\n");

    expect_range_iterator(actual, 2, 9, 3);
}

TEST(Interpreter, for_loop_iterates_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(Interpreter, for_loop_iterates_two_argument_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(1, 4):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(6), actual);
}

TEST(Interpreter, for_loop_iterates_positive_step_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(1, 8, 3):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(12), actual);
}

TEST(Interpreter, for_loop_iterates_negative_step_range)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5, -1, -2):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, direct_range_for_loop_reports_integer_argument_errors)
{
    expect_runtime_error(L"for x in range(False):\n"
                         L"    0\n",
                         "TypeError: range() arguments must be integers");
}

TEST(Interpreter, direct_range_for_loop_reports_zero_step_errors)
{
    expect_runtime_error(L"for x in range(1, 4, 0):\n"
                         L"    0\n",
                         "ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, for_else_runs_after_normal_exhaustion)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(3):\n"
                                 "    total += x\n"
                                 "else:\n"
                                 "    total += 10\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(13), actual);
}

TEST(Interpreter, for_else_skipped_after_break)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5):\n"
                                 "    if x == 3:\n"
                                 "        break\n"
                                 "    total += x\n"
                                 "else:\n"
                                 "    total += 100\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(3), actual);
}

TEST(Interpreter, for_continue_jumps_to_next_iteration)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(5):\n"
                                 "    if x == 2:\n"
                                 "        continue\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(8), actual);
}

TEST(Interpreter, nested_for_loops_execute_correctly)
{
    test::FileRunner file_runner(L"total = 0\n"
                                 "for x in range(3):\n"
                                 "    for y in range(2):\n"
                                 "        total += x + y\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, for_loop_executes_inside_function)
{
    test::FileRunner file_runner(L"def sum_range(n):\n"
                                 "    total = 0\n"
                                 "    for x in range(n):\n"
                                 "        total += x\n"
                                 "    return total\n"
                                 "sum_range(5)\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(10), actual);
}

TEST(Interpreter, nested_for_loops_execute_inside_function)
{
    test::FileRunner file_runner(L"def sum_pairs(n):\n"
                                 "    total = 0\n"
                                 "    for x in range(n):\n"
                                 "        for y in range(2):\n"
                                 "            total += x + y\n"
                                 "    return total\n"
                                 "sum_pairs(3)\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(9), actual);
}

TEST(Interpreter, shadowed_range_for_loop_uses_generic_fallback)
{
    test::FileRunner file_runner(L"real_range = range\n"
                                 "def range(n):\n"
                                 "    return real_range(1, n)\n"
                                 "total = 0\n"
                                 "for x in range(4):\n"
                                 "    total += x\n"
                                 "total\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(Value::from_smi(6), actual);
}

TEST(Interpreter, module_scope_can_shadow_builtin_scope)
{
    test::FileRunner file_runner(L"range = 42\n"
                                 "range\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(Value::from_smi(42), actual);
}

TEST(Interpreter, range_builtin_requires_integer_argument)
{
    expect_runtime_error(L"range(False)\n",
                         "TypeError: range() arguments must be integers");
    expect_runtime_error(L"range(1, False)\n",
                         "TypeError: range() arguments must be integers");
    expect_runtime_error(L"range(1, 2, False)\n",
                         "TypeError: range() arguments must be integers");
}

TEST(Interpreter, range_builtin_rejects_zero_step)
{
    expect_runtime_error(L"range(1, 2, 0)\n",
                         "ValueError: range() arg 3 must not be zero");
}

TEST(Interpreter, for_loop_rejects_non_iterable)
{
    expect_runtime_error(L"for x in 1:\n"
                         L"    x\n",
                         "TypeError: object is not iterable");
}

TEST(Interpreter, negate_expression)
{
    Value expected = Value::from_smi(-7);
    test::FileRunner file_runner(L"-(3 + 4)\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, left_shift_negative_count)
{
    expect_runtime_error(L"a = 1\n"
                         L"b = -1\n"
                         L"a << b\n",
                         "ValueError: negative shift count");
}

TEST(Interpreter, right_shift_negative_count)
{
    expect_runtime_error(L"a = 8\n"
                         L"b = -1\n"
                         L"a >> b\n",
                         "ValueError: negative shift count");
}

TEST(Interpreter, left_shift_boundary)
{
    Value expected = Value::from_smi(kMinSmi);
    test::FileRunner file_runner(L"(-1) << 58\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, left_shift_overflow_smi)
{
    expect_runtime_error(L"1 << 58\n", "Clovervm exception");
}

TEST(Interpreter, left_shift_overflow_register)
{
    expect_runtime_error(L"a = 1\n"
                         L"b = 58\n"
                         L"a << b\n",
                         "Clovervm exception");
}

TEST(Interpreter, right_shift_negative_value)
{
    Value expected = Value::from_smi(-5);
    test::FileRunner file_runner(L"(-9) >> 1\n");
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, add_overflow)
{
    expect_runtime_error(L"288230376151711743 + 1\n", "Clovervm exception");
}

TEST(Interpreter, subtract_overflow)
{
    expect_runtime_error(L"-288230376151711743 - 2\n", "Clovervm exception");
}

TEST(Interpreter, multiply_overflow)
{
    expect_runtime_error(L"288230376151711743 * 2\n", "Clovervm exception");
}

TEST(Interpreter, negate_overflow)
{
    Value expected = Value::from_smi(kMinSmi);
    test::FileRunner file_runner(L"-288230376151711743 - 1\n");
    Value actual = file_runner.return_value;
    EXPECT_EQ(expected, actual);

    expect_runtime_error(L"x = -288230376151711743 - 1\n"
                         L"-x\n",
                         "Clovervm exception");
}

/*
TEST(Interpreter, small_bench)
{
    const wchar_t *test_case =
        L"counter = 100000001\n"
        "acc = 1245\n"
        "while counter:\n"
        "    acc = (-acc*64 + 64)>>6\n"
        "    counter -= 1\n"
        "acc\n";

    Value expected = Value::from_smi(-1244);
    test::FileRunner file_runner(test_case);
    Value actual = file_runner.return_value;

    EXPECT_EQ(expected, actual);
}
*/
